/**
  ******************************************************************************
  * @file   nexserial.c
  * @author Onur Efe
  * @brief  Nexcore adapter source file for STM32 HAL driver. 
  */
/* Includes ----------------------------------------------------------------*/
#include "nexserial.h"
#include "queue.h"

/* Exported variables ------------------------------------------------------*/
extern UART_HandleTypeDef huart1;

/* Private variables -------------------------------------------------------*/
// Runtime variables.
static TaskStatus_t Status = UNINIT;
static volatile Bool_t TxActive;

// Buffers & containers.
static uint8_t TxBufferContainer[SERIAL_TX_BUFFER_SIZE];
static uint8_t RxBufferContainer[SERIAL_RX_BUFFER_SIZE];
static Queue_Buffer_t TxBuffer;
static Queue_Buffer_t RxBuffer;
static uint8_t TxReg;
static uint8_t RxReg;

/* Exported functions ------------------------------------------------------*/
/**
 * @brief  Function to initialize the module.
 */
void Serial_Init(void)
{
    // Guard for invalid operations.
    if (Status != UNINIT)
    {
        return;
    }

    // Initialize buffers.
    Queue_InitBuffer(&RxBuffer, RxBufferContainer, SERIAL_RX_BUFFER_SIZE);
    Queue_InitBuffer(&TxBuffer, TxBufferContainer, SERIAL_TX_BUFFER_SIZE);

    Status = READY;
}

/**
 * @brief Function to start the module.
 */
void Serial_Start(void)
{
    // Guard for invalid operations.
    if (Status != READY)
    {
        return;
    }

    // Clear buffers.
    Queue_ClearBuffer(&RxBuffer);
    Queue_ClearBuffer(&TxBuffer);

    // Start receiving data.
    if (HAL_UART_Receive_IT(&huart1, &RxReg, sizeof(RxReg)) != HAL_OK)
    {
        while (TRUE)
            ;
    }

    TxActive = FALSE;
    Status = OPERATING;
}

/**
 * @brief Function to stop the module.
 */
void Serial_Stop(void)
{
    // Guard for invalid operations.
    if (Status != OPERATING)
    {
        return;
    }

    HAL_UART_Abort_IT(&huart1);
    Status = READY;
}

/**
 * @brief Returns if there are any empty space in the TxBuffer.
 * 
 * @retval TRUE if there are any space; FALSE if not.
 */
Bool_t Serial_TxBuffAvailable(void)
{
    return (!Queue_IsFull(&TxBuffer));
}

/**
 * @brief Enqueues array of data to the TxBuffer and starts the
 * transmission if it's not active.
 * 
 * @param buff: Pointer of the array.
 * @param length: Element count of the array.
 */
void Serial_Transmit(uint8_t *buff, uint16_t length)
{
    // Enqueue data to the buffer.
    Queue_EnqueueArr(&TxBuffer, buff, length);

    // Start transmision if it wasn't active.
    if (!TxActive && !Queue_IsEmpty(&TxBuffer))
    {
        TxReg = Queue_Dequeue(&TxBuffer);

        if (HAL_UART_Transmit_IT(&huart1, &TxReg, sizeof(TxReg)) != HAL_OK)
        {
            while (TRUE)
                ;
        }

        TxActive = TRUE;
    }
}

/**
 * @brief Returns received data.
 * 
 * @param buff: Pointer of the buffer which the received data will be
 * transferred.
 * @param maxLength: Maximum length of data to be read.
 * 
 * @retval Length of the read.
 */
uint16_t Serial_GetRecvData(uint8_t *buff, uint16_t maxLength)
{
    uint16_t length;

    // Determine length of the read.
    length = Queue_GetElementCount(&RxBuffer);
    length = length > maxLength ? maxLength : length;

    // Dequeue the data from the input buffer.
    Queue_DequeueArr(&RxBuffer, buff, length);

    return length;
}

/* Callback functions ------------------------------------------------------*/
/**
 * @brief STM32 HAL Driver UART RX callback function implementation.
 * 
 * @param huart: Handle of the UART driver.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        // Guard for invalid operations.
        if (Status != OPERATING)
        {
            return;
        }

        // Restart reception interrupt.
        if (HAL_UART_Receive_IT(&huart1, &RxReg, sizeof(RxReg)) != HAL_OK)
        {
            while (TRUE)
                ;
        }

        // Enqueue received data.
        Queue_Enqueue(&RxBuffer, RxReg);
    }
}

/**
 * @brief STM32 HAL Driver UART TX callback function implementation.
 * 
 * @param huart: Handle of the UART driver.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        // Guard for invalid operations.
        if (Status != OPERATING)
        {
            return;
        }

        // If there are pending data on the TxBuffer; transmit it.
        if (!Queue_IsEmpty(&TxBuffer))
        {
            TxReg = Queue_Dequeue(&TxBuffer);

            // Restart transmission interrupt.
            if (HAL_UART_Transmit_IT(&huart1, &RxReg, sizeof(RxReg)) != HAL_OK)
            {
                while (TRUE)
                    ;
            }
        }
    }
}