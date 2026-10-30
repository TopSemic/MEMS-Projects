/************************************************************************************************/
/**
* @file               common.h
* @author             MCU Ecosystem Development Team
* @brief              COMMON头文件。
*                           
*
**************************************************************************************************
* @attention
*
**************************************************************************************************
*/

/* 避免头文件重复引用 */
#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------includes--------------------------------------------*/
#include "main.h"
#include <stdio.h>
/*-------------------------------------------functions------------------------------------------*/
void USART2_GPIO_Init(void);
void USART2_Init(void);
void LL_USART_SendString(USART_TypeDef *USARTx, const char *str);
    
#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */
