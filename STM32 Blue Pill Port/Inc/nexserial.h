/**
  ******************************************************************************
  * @file   nexserial.h
  * @author Onur Efe
  * @brief  Nexcore adapter header file for STM32 series MCU. 
  */

#ifndef __NEXSERIAL_H
#define __NEXSERIAL_H

/* Includes ----------------------------------------------------------------*/
#include "global.h"

/* Exported functions ------------------------------------------------------*/
// Module control functions.
void Serial_Init(void);
void Serial_Start(void);
void Serial_Stop(void);

// Functions used by nexcore module.
Bool_t Serial_TxBuffAvailable(void);
void Serial_Transmit(uint8_t *buff, uint16_t length);
uint16_t Serial_GetRecvData(uint8_t *buff, uint16_t maxLength);

#endif