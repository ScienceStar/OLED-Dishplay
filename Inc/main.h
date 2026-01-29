#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* Exported functions prototypes */
void Error_Handler(void);

/* ================= UART & ESP8266 ================= */
extern uint8_t UartTxbuf[1000];
extern uint8_t UartRxbuf[1024], UartIntRxbuf[1024];
extern uint16_t UartRxIndex;
extern uint16_t UartRxFlag;
extern uint16_t UartRxLen;
extern uint16_t UartRxTimer;
extern uint16_t UartRxOKFlag;
extern uint16_t UartIntRxLen;

extern uint8_t Uart_RecvFlag(void);
extern uint8_t UartRecv_Clear(void);
extern void UART_RecvDealwith(void);

/* ================= GPIO ================= */
#define LED_Pin         GPIO_PIN_13
#define LED_GPIO_Port   GPIOC
#define LEDB4_Pin       GPIO_PIN_4
#define LEDB4_GPIO_Port GPIOB

/* ================= Morse/OLED ================= */
extern void LED_Morse_NonBlocking(void);
extern void OLED_Scroll_NonBlocking(void);
extern void OLED_Init(void);
extern void OLED_Clear(void);
extern void OLED_ShowString(uint8_t x,uint8_t y,uint8_t *p);

/* ================= ESP8266 TCP ================= */
extern void ESP8266_Init(void);
extern void ESP8266_STA_TCPClient_Test(void);
extern void TCP_ProcessData(uint8_t *buf, uint16_t len);

/* ================= System ================= */
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */