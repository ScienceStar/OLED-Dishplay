/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* ================= UART & TCP ================= */
extern uint8_t UartTxbuf[1000];
extern uint8_t UartRxbuf[1024], UartIntRxbuf[1024];
extern uint16_t UartRxIndex, UartRxFlag, UartRxLen, UartRxTimer, UartRxOKFlag, UartIntRxLen;
extern uint8_t Uart_RecvFlag(void);
extern void UartRecv_Clear(void);           // 改为 void
extern void UART_RecvDealwith(void);        // 声明 UART 轮询处理函数

/* ================= GPIO ================= */
#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC
#define LEDB4_Pin GPIO_PIN_4
#define LEDB4_GPIO_Port GPIOB

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */