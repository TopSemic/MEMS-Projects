/************************************************************************************************/
/**
* @file               common.c
* @author             MCU Ecosystem Development Team
* @brief              通用函数或本外设相关的配置实现函数。
*
*
**************************************************************************************************
* @attention
* Copyright (c) CEC Huada Electronic Design Co.,Ltd. All rights reserved.
*
**************************************************************************************************
*/

/*------------------------------------------includes--------------------------------------------*/
#include "common.h"

/*------------------------------------------functions-------------------------------------------*/
/**
 * @brief USART2引脚初始化：PA2(TX)、PA3(RX)
 */
void USART2_GPIO_Init(void)
{
  /* 1. 使能GPIOA时钟（AHB2总线） */
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);

  /* 2. 配置PA2为USART2_TX */
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;       // 复用模式
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;     // 高速
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;// 推挽输出
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;              // 无上下拉
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;            // 复用为USART2（AF7）
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 3. 配置PA3为USART2_RX */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;              // 浮空输入（也可改为上拉）
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
 * @brief USART2外设初始化：115200 8N1
 */
void USART2_Init(void)
{
  /* 1. 使能USART2时钟（APB1总线） */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

  /* 2. 配置USART参数 */
  LL_USART_InitTypeDef USART_InitStruct = {0};
  USART_InitStruct.BaudRate = 115200;                     // 波特率
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;     // 8位数据位
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;        // 1位停止位
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;         // 无校验
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX; // 收发使能
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE; // 无流控
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;      // 16倍过采样
  LL_USART_Init(USART2, &USART_InitStruct);

  /* 3. 使能USART2 */
  LL_USART_Enable(USART2);
}

/**
 * @brief LL库串口单字节发送（阻塞式）
 * @param USARTx：USART外设基地址（如USART1）
 * @param data：待发送的字节
 */
static inline void LL_USART_SendByte(USART_TypeDef *USARTx, uint8_t data)
{
  /* 等待发送数据寄存器为空（TXE标志置位） */
  while (!LL_USART_IsActiveFlag_TXE(USARTx));
  /* 写入数据到发送寄存器 */
  LL_USART_TransmitData8(USARTx, data);
}

/**
 * @brief LL库串口字符串发送（阻塞式）
 * @param USARTx：USART外设基地址
 * @param str：待发送的字符串
 */
void LL_USART_SendString(USART_TypeDef *USARTx, const char *str)
{
  while (*str != '\0')
  {
    LL_USART_SendByte(USARTx, (uint8_t)*str);
    str++;
  }
  /* 可选：等待最后一个字节发送完成（TC标志置位） */
  while (!LL_USART_IsActiveFlag_TC(USARTx));
}


/**
* @brief  重定向c库函数printf到串口，重定向后可使用printf函数
* @param  ch 待发送字符
* @retval ch 发送的字符
*/
int fputc(int ch, FILE *f)
{
    /* 发送一个字节数据到串口 */
    LL_USART_SendByte(USART2, (uint32_t)ch);
    
    /* 等待发送完毕 */
    while(!LL_USART_IsActiveFlag_TC(USART2));
    return ch;
}
