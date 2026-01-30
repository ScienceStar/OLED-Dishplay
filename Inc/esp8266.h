#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* ================= 配置 ================= */
#define ESP8266_UART     huart2
#define ESP8266_RX_MAX   1024

/* ================= 枚举 ================= */
typedef enum { STA = 1, AP = 2, STA_AP = 3 } ESP8266_Mode;
typedef enum { enumTCP, enumUDP } ESP8266_NetProto;

/* ================= 状态变量 ================= */
extern uint8_t  esp8266_rx_buf[ESP8266_RX_MAX];
extern uint16_t esp8266_rx_len;
extern volatile uint8_t esp8266_rx_ok;

/* ================= 基础接口 ================= */
void ESP8266_Init(void);
bool ESP8266_AT_Test(void);
bool ESP8266_SetMode(ESP8266_Mode mode);
bool ESP8266_JoinAP(char *ssid, char *pwd);

/* ================= TCP ================= */
bool ESP8266_TCP_Connect(char *ip, uint16_t port);
bool ESP8266_TCP_Send(char *data);
bool ESP8266_TCP_EnterTransparent(void);
void ESP8266_TCP_ExitTransparent(void);

/* ================= 工具 ================= */
void ESP8266_ClearRx(void);
bool ESP8266_WaitReply(char *ack, uint32_t timeout);

#endif