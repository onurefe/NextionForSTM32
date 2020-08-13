/***
  * @file   nexcore.c
  * @author Onur Efe
  * @brief  Nextion HMI core function implementations.
  * 
  @verbatim
  ==============================================================================
                            ##### How to use #####
  ==============================================================================
    In order to use this library there should be serial interface driver which 
    provides;
        -Bool_t Serial_TxBuffAvailable(void)
        -void Serial_Transmit(uint8_t *buff, uint16_t length);
        -uint16_t Serial_GetRecvData(uint8_t *buff, uint16_t maxLength);
    functions. The user should implement his own serial.c and serial.h files
    to adapt this code to his architecture. Example portage for STM32F1 is
    is given in the repository. 
  @endverbatim
*/

#include "nexcore.h"
#include "nexserial.h"
#include "utilities.h"
#include "queue.h"

/* Private definitions -----------------------------------------------------*/
// Nextion HMI return codes.
#define NR_INVALID_INSTRUCTION ((uint8_t)0x00)
#define NR_INSTRUCTION_SUCCESSFUL ((uint8_t)0x01)
#define NR_INVALID_COMPONENT_ID ((uint8_t)0x02)
#define NR_INVALID_PAGE_ID ((uint8_t)0x03)
#define NR_INVALID_PICTURE_ID ((uint8_t)0x04)
#define NR_INVALID_FONT_ID ((uint8_t)0x05)
#define NR_INVALID_FILE_OPERATION ((uint8_t)0x06)
#define NR_INVALID_CRC ((uint8_t)0x09)
#define NR_INVALID_BAUDRATE_SETTING ((uint8_t)0x11)
#define NR_INVALID_WAVEFORM_ID_OR_CHANNEL ((uint8_t)0x12)
#define NR_INVALID_VARIABLE_NAME_OR_ATTIRIBUTE ((uint8_t)0x1A)
#define NR_INVALID_VARIABLE_OPERATION ((uint8_t)0x1B)
#define NR_ASSIGNMENT_FAILED_TO_ASSIGN ((uint8_t)0x1C)
#define NR_EEPROM_OPERATION_FAILED ((uint8_t)0x1D)
#define NR_INVALID_QUANTITY_OF_PARAMETERS ((uint8_t)0x1E)
#define NR_IO_OPERATION_FAILED ((uint8_t)0x1F)
#define NR_ESCAPE_CHARACTER_INVALID ((uint8_t)0x20)
#define NR_VARIABLE_NAME_TOO_LONG ((uint8_t)0x23)
#define NR_SERIAL_BUFFER_OVERFLOW ((uint8_t)0x24)
#define NR_TOUCH_EVENT ((uint8_t)0x65)
#define NR_CURRENT_PAGE_NUMBER ((uint8_t)0x66)
#define NR_TOUCH_COORDINATE_AWAKE ((uint8_t)0x67)
#define NR_TOUCH_COORDINATE_SLEEP ((uint8_t)0x68)
#define NR_VARIED_STRING_DATA_ENCLOSED ((uint8_t)0x70)
#define NR_NUMERIC_DATA_ENCLOSED ((uint8_t)0x71)
#define NR_AUTO_ENTERED_SLEEP_MODE ((uint8_t)0x86)
#define NR_AUTO_WAKE_FROM_SLEEP ((uint8_t)0x87)
#define NR_NEXTION_READY ((uint8_t)0x88)
#define NR_START_MICROSD_UPGRAGE ((uint8_t)0x89)
#define NR_TRANSPARENT_DATA_FINISHED ((uint8_t)0xFD)
#define NR_TRANSPARENT_DATA_READY ((uint8_t)0xFE)

/* Private function prototypes ---------------------------------------------*/
static void messageReceived(uint8_t messageLength);

/* Exported variables ------------------------------------------------------*/
extern UART_HandleTypeDef huart1;

/* Private variables -------------------------------------------------------*/
// Run-time variables.
static TaskStatus_t Status = UNINIT;
static Bool_t Busy;
static uint32_t ResponseStartTick;
static uint16_t ActiveTransactionBytesToSend;
static uint8_t TransactionNumerator;
static uint8_t ActiveTransactionId;

// Data buffers and data structures.
static uint8_t TxQueueContainer[NEXCORE_TX_QUEUE_SIZE];
static Queue_Buffer_t TxQueue;
static uint8_t RxQueueContainer[NEXCORE_RX_QUEUE_SIZE];
static Queue_Buffer_t RxQueue;

// Constant data.
static uint8_t MessageTerminator[] = {0xFF, 0xFF, 0xFF};

/* Exported functions ------------------------------------------------------*/
/**
 * @brief Initializes module. 
 */
void Nexcore_Init(void)
{
    if (Status != UNINIT)
    {
        return;
    }

    Queue_InitBuffer(&TxQueue, TxQueueContainer, NEXCORE_TX_QUEUE_SIZE);
    Queue_InitBuffer(&RxQueue, RxQueueContainer, NEXCORE_RX_QUEUE_SIZE);

    Status = READY;
}

/**
 * @brief Starts the operation of the module.
 */
void Nexcore_Start(void)
{
    if (Status != READY)
    {
        return;
    }

    // Clear RX and TX queues.
    Queue_ClearBuffer(&RxQueue);
    Queue_ClearBuffer(&TxQueue);

    // Start runtime variables.
    Busy = FALSE;
    TransactionNumerator = 0;

    // Enqueue command response type setting operation.
    char sys_variable[] = "bkcmd";
    Nexcore_Set(sys_variable, NULL, "3");

    Status = OPERATING;
}

/**
 * @brief This function should be called to transfer control
 * to this module to complete it's pending operations.
 */
void Nexcore_Execute(void)
{
    if (Status != OPERATING)
    {
        return;
    }

    if (Busy)
    {
        // Transmit the data, if there are bytes to send.
        if (ActiveTransactionBytesToSend)
        {
            // Enqueue data to serial tx buffer.
            while (ActiveTransactionBytesToSend)
            {
                if (!Serial_TxBuffAvailable())
                {
                    break;
                }

                uint8_t tfreg;
                if (ActiveTransactionBytesToSend > sizeof(MessageTerminator))
                {
                    tfreg = Queue_Dequeue(&TxQueue);
                }
                else
                {
                    tfreg = MessageTerminator[sizeof(MessageTerminator) - ActiveTransactionBytesToSend];
                }

                Serial_Transmit(&tfreg, sizeof(tfreg));
                ActiveTransactionBytesToSend--;
            }

            if (ActiveTransactionBytesToSend == 0)
            {
                // Get rid of null element.
                Queue_Dequeue(&TxQueue);
                ResponseStartTick = HAL_GetTick();
            }
        }

        // Check for transaction timeout if all the bytes are either sent or
        //transfered to the transmit buffer.
        if (!ActiveTransactionBytesToSend)
        {
            if (HAL_GetTick() - ResponseStartTick > NEXCORE_TRANSACTION_TIMEOUT)
            {
                // Skip this transaction and invoke no response error.
                Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_NO_RESPONSE);
                Busy = FALSE;
            }
        }
    }
    else
    {
        uint16_t message_end_idx;
        message_end_idx = Queue_Search(&TxQueue, '\0');

        // If message end has been found;
        if (message_end_idx != 0xFFFF)
        {
            // Parse active transaction id.
            ActiveTransactionId = Queue_Dequeue(&TxQueue);
            message_end_idx--;

            // Set bytes to send.
            ActiveTransactionBytesToSend = message_end_idx + sizeof(MessageTerminator);
            Busy = TRUE;
        }
    }

    // Check if any message received.
    {
        uint16_t message_length;
        message_length = Queue_SearchArr(&RxQueue, MessageTerminator,
                                         sizeof(MessageTerminator));

        // Search for terminate flag.
        if (message_length != 0xFFFF)
        {
            // If the terminate flag is found, evaluate
            //received message(terminator not included in length).
            messageReceived(message_length);

            // If it was busy; clear the busy flag.
            if (Busy)
            {
                Busy = FALSE;
            }
        }
    }
}

/**
 * @brief Stops the module.
 */
void Nexcore_Stop(void)
{
    if (Status != OPERATING)
    {
        return;
    }

    HAL_UART_Abort_IT(&huart1);
    Status = READY;
}

/**
 * @brief Concats the strings given as chunks and enqueues them
 * to the module's TxQueue. 
 * 
 * @param chunks: Array of pointers to the strings. 
 * @param chunkCount: Number of chunks to be concatted.
 * 
 * @retval TransactionId. 
 */
uint8_t Nexcore_EnqueueMsg(char **chunks, uint8_t chunkCount)
{
    // Enqueue transaction numerator.
    Queue_Enqueue(&TxQueue, TransactionNumerator);

    // Enqueue chunks(the strings which the message is composed of).
    for (uint8_t i = 0; i < chunkCount; i++)
    {
        Queue_EnqueueArr(&TxQueue, (uint8_t *)chunks[i], strLen(chunks[i]));
    }

    // Enqueue null character.
    Queue_Enqueue(&TxQueue, '\0');

    return TransactionNumerator++;
}

/**
 * @brief Gets the value of a component's attribute or a system variable.
 * 
 * @param name: Name of the component or the system variable.
 * @param attribute: Name of the component's attribute. NULL pointer 
 * if system variable is to be get.
 * 
 * @retval TransactionId.
 */
uint8_t Nexcore_Get(char *name, char *attribute)
{
    uint8_t idx = 0;
    char *chunks[4];
    char cmd[] = "get ";
    char op_dot[] = ".";

    chunks[idx++] = cmd;
    chunks[idx++] = name;
    if (attribute)
    {
        chunks[idx++] = op_dot;
        chunks[idx++] = attribute;
    }

    return Nexcore_EnqueueMsg(chunks, idx);
}

/**
 * @brief Sets the value of a component's attribute or a system variable.
 * 
 * @param name: Name of the component or the system variable.
 * @param attribute: Name of the component's attribute. NULL pointer 
 * if system variable is to be set.
 * 
 * @retval TransactionId.
 */
uint8_t Nexcore_Set(char *componentName, char *attribute, char *txt)
{
    uint8_t idx = 0;
    char *chunks[5];
    char op_dot[] = ".";
    char op_assign[] = "=";

    chunks[idx++] = componentName;

    if (attribute)
    {
        chunks[idx++] = op_dot;
        chunks[idx++] = attribute;
    }

    chunks[idx++] = op_assign;
    chunks[idx++] = txt;

    return Nexcore_EnqueueMsg(chunks, idx);
}

/**
 * @brief Changes active page.
 * 
 * @param pageName: Name of the page to be activated.
 * 
 * @retval TransactionId.
 */
uint8_t Nexcore_Page(char *pageName)
{
    char *chunks[2];
    char cmd[] = "page ";

    chunks[0] = cmd;
    chunks[1] = pageName;

    return Nexcore_EnqueueMsg(chunks, 2);
}

/**
 * @brief Makes a component visible or invisible.
 * 
 * @param componentName: Name of the component.
 * @param visible: Set FALSE to make a component invisible. 
 * Set TRUE to make a component visible.
 * 
 * @retval TransactionId.
 */
uint8_t Nexcore_Visible(char *componentName, Bool_t visible)
{
    char *chunks[2];
    char cmd[] = "vis ";
    char visible_str[] = "0";

    visible_str[0] = visible ? '1' : '0';
    chunks[0] = cmd;
    chunks[1] = visible_str;

    return Nexcore_EnqueueMsg(chunks, 2);
}

/** 
 * @brief Adds point to waveform component's channel.
 * 
 * @param componentId: Id of the component.
 * @param channel: Channel which the point will be added.
 * @param y: Height of the point.
 * 
 * @retval TransactionId.
 */
uint8_t Nexcore_AddPointToWaveform(uint8_t componentId, uint8_t channel, uint16_t y)
{
    char *chunks[4];
    char cmd[] = "add ";
    char comp_id_str[4];
    char channel_str[4];
    char y_str[6];

    // Convert numbers to strings.
    num2str(componentId, comp_id_str);
    num2str(channel, channel_str);
    num2str(y, y_str);

    chunks[0] = cmd;
    chunks[1] = comp_id_str;
    chunks[2] = channel_str;
    chunks[3] = y_str;

    return Nexcore_EnqueueMsg(chunks, 4);
}

/** 
 * @brief Clear waveform component's channel.
 * 
 * @param componentId: Id of the component.
 * @param channel: Channel which will be cleared.
 * 
 * @retval TransactionId.
 */
uint8_t Nexcore_ClearWaveform(uint8_t componentId, uint8_t channel)
{
    char *chunks[3];
    char cmd[] = "cle ";
    char comp_id_str[4];
    char channel_str[4];

    // Convert numbers to strings.
    num2str(componentId, comp_id_str);
    num2str(channel, channel_str);

    chunks[0] = cmd;
    chunks[1] = comp_id_str;
    chunks[2] = channel_str;

    return Nexcore_EnqueueMsg(chunks, 3);
}

/**
 * @brief Resets the screen.
 */
uint8_t Nexcore_Reset(void)
{
    char *chunks[1];
    char cmd[] = "rest";

    chunks[0] = cmd;

    return Nexcore_EnqueueMsg(chunks, 1);
}

/**
 * @brief After receiving string return data; callback is invoked to
 * inform this to the application program. Then the application program should
 * call this function to parse the string from the internal receive buffer
 * of the module. String can be parsed as chunks of strings or as a whole at a time.
 * 
 * @param buff: Pointer of the buffer to put received string.
 * @param maxLength: Length of the parsing.
 * 
 * @retval Characters to parse.
 */
uint8_t Nexcore_ParseReturnedString(char *buff, uint8_t maxLength)
{
    uint8_t characters_to_parse;
    uint8_t parse_length;
    characters_to_parse = Queue_SearchArr(&RxQueue, MessageTerminator,
                                          sizeof(MessageTerminator));
    // Determine the parse length.
    parse_length = ((maxLength - 1) > characters_to_parse) ? characters_to_parse : (maxLength - 1);

    // Read string.
    Queue_DequeueArr(&RxQueue, (uint8_t *)buff, parse_length);
    buff[parse_length] = '\0';

    characters_to_parse -= parse_length;

    // If all the data is parsed, remove the terminate flag.
    if (characters_to_parse == 0)
    {
        // Remove terminate flag.
        Queue_Remove(&RxQueue, sizeof(MessageTerminator));
    }

    return characters_to_parse;
}

/* Private functions -------------------------------------------------------*/
/**
 * @brief Called when terminator flag is detected. This function deserializes
 * the received message and triggers required callbacks.
 * 
 * @param length: Length of the message received.
 */
void messageReceived(uint8_t length)
{
    uint8_t bytes_to_parse = length;
    uint8_t ret_code;
    ret_code = Queue_Dequeue(&RxQueue);
    bytes_to_parse--;

    switch (ret_code)
    {
    case NR_TRANSPARENT_DATA_READY:
    case NR_TRANSPARENT_DATA_FINISHED:
    case NR_START_MICROSD_UPGRAGE:
    case NR_AUTO_ENTERED_SLEEP_MODE:
    case NR_AUTO_WAKE_FROM_SLEEP:
    case NR_TOUCH_COORDINATE_AWAKE:
    case NR_TOUCH_COORDINATE_SLEEP:
    case NR_CURRENT_PAGE_NUMBER:
    case NR_INVALID_CRC:
    case NR_INVALID_FILE_OPERATION:
    case NR_EEPROM_OPERATION_FAILED:
    case NR_IO_OPERATION_FAILED:
        break;

    case NR_INVALID_INSTRUCTION:
    case NR_INVALID_COMPONENT_ID:
    case NR_INVALID_PAGE_ID:
    case NR_INVALID_PICTURE_ID:
    case NR_INVALID_FONT_ID:
    case NR_INVALID_WAVEFORM_ID_OR_CHANNEL:
    case NR_INVALID_VARIABLE_NAME_OR_ATTIRIBUTE:
    case NR_INVALID_VARIABLE_OPERATION:
    case NR_ASSIGNMENT_FAILED_TO_ASSIGN:
    case NR_INVALID_QUANTITY_OF_PARAMETERS:
    case NR_VARIABLE_NAME_TOO_LONG:
    {
        if (bytes_to_parse != 0)
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_RECV_MSG);
            Queue_Remove(&RxQueue, bytes_to_parse);
        }
        else
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_OPERATION);
        }
    }
    break;

    case NR_ESCAPE_CHARACTER_INVALID:
    case NR_INVALID_BAUDRATE_SETTING:
    {
        if (bytes_to_parse != 0)
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_RECV_MSG);
            Queue_Remove(&RxQueue, bytes_to_parse);
        }
        else
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_COMMUNICATION_PROBLEM);
        }
    }
    break;

    case NR_SERIAL_BUFFER_OVERFLOW:
    {
        if (bytes_to_parse != 0)
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_RECV_MSG);
            Queue_Remove(&RxQueue, bytes_to_parse);
        }
        else
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_BUFFER_OVERFLOW);
        }
    }
    break;

    case NR_NEXTION_READY:
    {
        if (bytes_to_parse != 0)
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_RECV_MSG);
            Queue_Remove(&RxQueue, bytes_to_parse);
        }
        else
        {
            Nexcore_InfoCb(ActiveTransactionId, NEXCORE_INFO_READY);
        }
    }
    break;

    case NR_INSTRUCTION_SUCCESSFUL:
    {
        if (bytes_to_parse != 0)
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_RECV_MSG);
            Queue_Remove(&RxQueue, bytes_to_parse);
        }
        else
        {
            Nexcore_InfoCb(ActiveTransactionId, NEXCORE_INFO_INSTRUCTION_SUCCESSFUL);
        }
    }
    break;

    case NR_TOUCH_EVENT:
    {
        if (bytes_to_parse != 3)
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_RECV_MSG);
            Queue_Remove(&RxQueue, bytes_to_parse);
        }
        else
        {
            uint8_t page_id, comp_id;
            Bool_t pressed;
            page_id = Queue_Dequeue(&RxQueue);
            comp_id = Queue_Dequeue(&RxQueue);
            pressed = (Queue_Dequeue(&RxQueue) == 0x01) ? TRUE : FALSE;
            Nexcore_TouchEventCb(ActiveTransactionId, page_id, comp_id, pressed);
        }
    }
    break;

    case NR_VARIED_STRING_DATA_ENCLOSED:
    {
        Nexcore_GetStringRspCb(ActiveTransactionId, bytes_to_parse);
    }
    break;

    case NR_NUMERIC_DATA_ENCLOSED:
    {
        if (bytes_to_parse != 4)
        {
            Nexcore_ErrorCb(ActiveTransactionId, NEXCORE_ERROR_INVALID_RECV_MSG);
            Queue_Remove(&RxQueue, bytes_to_parse);
        }
        else
        {
            uint8_t bytes[sizeof(uint32_t)];
            uint32_t value;
            Queue_DequeueArr(&RxQueue, bytes, sizeof(bytes));
            value = bytes[0] + (bytes[1] << 8) + (bytes[2] << 16) + (bytes[3] << 24);
            Nexcore_GetNumberRspCb(ActiveTransactionId, value);
        }
    }
    break;
    }

    // If nothing to parse; get rid of the terminator bytes in the buffer.
    if (bytes_to_parse == 0)
    {
        Queue_Remove(&RxQueue, sizeof(MessageTerminator));
    }
}

/* Default callbacks -------------------------------------------------------*/
/**
 * @brief Default implementation of the error callback function.
 * 
 * @param activeTransactionId: Current transaction ID.
 * @param error: Error code.
 */
__weak void Nexcore_ErrorCb(uint8_t activeTransactionId, Nexcore_Error_t error)
{
}

/**
 * @brief Default implementation of the info callback function.
 * 
 * @param activeTransactionId: Current transaction ID.
 * @param error: Info code.
 */
__weak void Nexcore_InfoCb(uint8_t activeTransactionId, Nexcore_Info_t info)
{
}

/**
 * @brief Default implementation of the touch event callback function.
 * 
 * @param activeTransactionId: Current transaction ID.
 * @param pageId: Active page ID.
 * @param componentId: ID of the component whom touch event is triggered.
 * @param pressed: Touch event type; TRUE if pressed FALSE if released.
 */
__weak void Nexcore_TouchEventCb(uint8_t activeTransactionId, uint8_t pageId,
                                 uint8_t componentId, Bool_t pressed)
{
}

/**
 * @brief Default implementation of the get(string attribute) response callback function.
 * Nexcore_ParseReturnedString function should be called after this function to parse the
 * returned string.
 * 
 * @param activeTransactionId: Current transaction ID.
 * @param stringLength: Length of the returned string.
 */
__weak void Nexcore_GetStringRspCb(uint8_t activeTransactionId, uint8_t stringLength)
{
}

/**
 * @brief Default implementation of the get(numeric attribute) response callback function.
 * 
 * @param activeTransactionId: Current transaction ID.
 * @param value: Returned value.
 */
__weak void Nexcore_GetNumberRspCb(uint8_t ActiveTransactionId, uint32_t value)
{
}