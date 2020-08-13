/**
  ******************************************************************************
  * @file   utilities.c
  * @author Onur Efe
  * @brief  Utility functions for general purpose.
  */

#include "utilities.h"

/* Exported functions ------------------------------------------------------*/
/**
 * @brief Converts an unsigned long integer to string.
 * 
 * @param num: Unsigned long integer.
 * @param str: Pointer to return string.
 */
void num2str(uint32_t num, char *str)
{
    uint8_t digits = 0;
    uint8_t temp;

    // Obtain digits.
    while (num > 0)
    {
        str[digits++] = 0x30 + (num % 10);
        num /= 10;
    }

    // Correct(reverse) order of the digits.
    for (uint8_t i = 0; i < digits; i++)
    {
        temp = str[i];
        str[i] = str[digits - (i + 1)];
        str[digits - (i + 1)] = temp;
    }

    // Add the null character.
    str[digits] = '\0';
}

/**
 * @brief Gets string length.
 * 
 * @param str: Pointer of the string.
 * 
 * @retval Length of the string.
 */
uint16_t strLen(char *str)
{
    uint8_t i = 0;
    uint16_t length = 0;
    while (str[i] != '\0')
    {
        length++;
    }

    return length;
}