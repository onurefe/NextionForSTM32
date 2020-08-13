/***
  * @file nexcore.h
  * @author Onur Efe
  * @brief  Nextion HMI core function declerations.
  */
#ifndef __NEXCORE_H
#define __NEXCORE_H

/* Includes ----------------------------------------------------------------*/
#include "global.h"

/* Exported definitions ----------------------------------------------------*/
// Error codes.
enum
{
  NEXCORE_ERROR_INVALID_OPERATION = 0,
  NEXCORE_ERROR_COMMUNICATION_PROBLEM,
  NEXCORE_ERROR_BUFFER_OVERFLOW,
  NEXCORE_ERROR_NO_RESPONSE,
  NEXCORE_ERROR_INVALID_RECV_MSG
};
typedef uint8_t Nexcore_Error_t;

enum
{
  NEXCORE_INFO_READY = 0,
  NEXCORE_INFO_INSTRUCTION_SUCCESSFUL
};
typedef uint8_t Nexcore_Info_t;

/* Exported functions ------------------------------------------------------*/
void Nexcore_Init(void);
void Nexcore_Start(void);
void Nexcore_Execute(void);
void Nexcore_Stop(void);
uint8_t Nexcore_ParseReturnedString(char *buff, uint8_t maxLength);
uint8_t Nexcore_EnqueueMsg(char **chunks, uint8_t chunkCount);
uint8_t Nexcore_Page(char *pageName);
uint8_t Nexcore_Get(char *componentName, char *attribute);
uint8_t Nexcore_Set(char *componentName, char *attribute, char *txt);
uint8_t Nexcore_Visible(char *componentName, Bool_t visible);
uint8_t Nexcore_AddPointToWaveform(uint8_t componentId, uint8_t channel, uint16_t y);
uint8_t Nexcore_ClearWaveform(uint8_t componentId, uint8_t channel);
uint8_t Nexcore_Reset(void);

/* Callbacks ---------------------------------------------------------------*/
void Nexcore_ErrorCb(uint8_t activeTransactionId, Nexcore_Error_t error);
void Nexcore_InfoCb(uint8_t activeTransactionId, Nexcore_Info_t info);
void Nexcore_TouchEventCb(uint8_t activeTransactionId, uint8_t pageId,
                          uint8_t componentId, Bool_t pressed);
void Nexcore_GetStringRspCb(uint8_t activeTransactionId, uint8_t stringLength);
void Nexcore_GetNumberRspCb(uint8_t activeTransactionId, uint32_t value);

#endif