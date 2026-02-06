#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* ================= 配置 ================= */
#define ESP8266_UART       huart2
#define ESP8266_RX_MAX     1024
#define ESP8266_LINE_MAX   128
#define ESP8266_LINE_NUM   8
#define WIFI_RECONNECT_INTERVAL 5000

/* ================= 行缓冲 ================= */
typedef struct {
    char line[ESP8266_LINE_MAX];
    uint16_t len;
    volatile uint8_t ready;
} ESP8266_Line;

/* ================= 对外变量 ================= */
extern uint8_t  esp8266_rx_byte;
extern ESP8266_Line esp8266_lines[ESP8266_LINE_NUM];
extern volatile uint8_t esp8266_line_write_index;
extern volatile uint8_t esp8266_line_read_index;
extern uint8_t UartRxData;
extern uint8_t UartRxbuf[1024], UartIntRxbuf[1024];
extern uint16_t UartRxIndex, UartRxFlag, UartRxLen, UartRxOKFlag, UartIntRxLen;
/* ================== WiFi 自动重连 ================== */
extern uint32_t wifi_reconnect_tick;
extern volatile uint8_t WiFiStatus; // 0=断开,1=连接
extern int8_t WiFiRSSI;

/* （保留给 MQTT / 原始数据用） */
extern uint8_t  esp8266_rx_buf[ESP8266_RX_MAX];
extern volatile uint16_t esp8266_rx_len;
/* ================== ESP8266 接收标志 ================== */
extern volatile uint8_t esp8266_rx_ok;

/* ================= WiFi / TCP ================= */
typedef enum {
    STA = 1,
    AP,
    STA_AP
} ESP8266_Mode;

bool ESP8266_Init(void);
bool ESP8266_AT_Test(void);
bool ESP8266_SetMode(ESP8266_Mode mode);
bool ESP8266_JoinAP(const char *ssid, const char *pwd);
void ESP_WiFi_ReconnectTask(void);
bool ESP8266_IsWiFiConnected(void);
uint8_t ESP8266_IsConnected(void);
bool ESP8266_GetRSSI(void);
bool ESP8266_TCP_Connect(const char *ip, uint16_t port);
bool ESP8266_TCP_Send(const char *data);
bool ESP8266_TCP_Close(void);
bool ESP8266_SendRaw(uint8_t *data, uint16_t len);
bool ESP8266_SendString(const char *str);
bool ESP8266_WaitResponse(const char *ack,
                          uint32_t timeout);
void ESP8266_ClearRx(void);

/* ================= 核心工具 ================= */
void ESP8266_ClearRx(void);
bool ESP8266_SendAndWait(const char *cmd,
                         const char *ack,
                         uint32_t timeout);

/* ================= 透传模式 ================= */
bool ESP8266_TCP_EnterTransparent(void);
void ESP8266_TCP_ExitTransparent(void);   // ? 必须有

/* ================= 接收处理 ================= */
void ESP8266_RxHandler(uint8_t ch);

/* ================= MQTT ================= */
void ESP8266_MQTT_Init(void);
bool ESP8266_MQTT_HasMsg(void);
char *ESP8266_MQTT_GetPayload(void);

#endif