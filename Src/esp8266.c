#include "esp8266.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "mqtt.h"

/* ================= 全局状态 ================= */
uint8_t esp8266_rx_buf[ESP8266_RX_MAX];
uint16_t esp8266_rx_len        = 0;
volatile uint8_t esp8266_rx_ok = 0;

/* ================= 清空接收缓冲 ================= */
void ESP8266_ClearRx(void)
{
    esp8266_rx_len = 0;
    memset(esp8266_rx_buf, 0, ESP8266_RX_MAX);
}

/* ================= 串口发送 + 等待 ================= */
bool ESP8266_SendAndWait(const char *cmd, const char *ack, uint32_t timeout)
{
    ESP8266_ClearRx();
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t *)cmd, strlen(cmd), 1000);

    uint32_t tick_start = HAL_GetTick();
    while ((HAL_GetTick() - tick_start) < timeout) {
        if (strstr((char *)esp8266_rx_buf, ack)) {
            return true;
        }
    }
    return false;
}

/* ================= 等待指定回复 ================= */
bool ESP8266_WaitReply(const char *ack, uint32_t timeout)
{
    uint32_t tick_start = HAL_GetTick();
    while ((HAL_GetTick() - tick_start) < timeout) {
        if (strstr((char *)esp8266_rx_buf, ack)) {
            return true;
        }
    }
    return false;
}

/* ================= ESP8266 初始化 ================= */
void ESP8266_Init(void)
{
    HAL_Delay(100);
    ESP8266_ClearRx();
}

/* ================= AT 测试 ================= */
bool ESP8266_AT_Test(void)
{
    return ESP8266_SendAndWait("AT\r\n", "OK", 1000);
}

/* ================= 设置模式 ================= */
bool ESP8266_SetMode(ESP8266_Mode mode)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d\r\n", mode);
    return ESP8266_SendAndWait(cmd, "OK", 1000);
}

/* ================= 连接 WiFi ================= */
bool ESP8266_JoinAP(const char *ssid, const char *pwd)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    return ESP8266_SendAndWait(cmd, "WIFI GOT IP", 5000);
}

/* ================= TCP 连接 ================= */
bool ESP8266_TCP_Connect(const char *ip, uint16_t port)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ip, port);
    return ESP8266_SendAndWait(cmd, "CONNECT", 5000);
}

/* ================= TCP 发送 ================= */
bool ESP8266_TCP_Send(const char *data)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", (int)strlen(data));
    if (!ESP8266_SendAndWait(cmd, ">", 1000)) return false;
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t *)data, strlen(data), 1000);
    return ESP8266_WaitReply("SEND OK", 1000);
}

bool ESP8266_SendRaw(uint8_t *data, uint16_t len)
{
    char cmd[32];

    /* 1. 发送 CIPSEND */
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);

    if (!ESP8266_SendCmd(cmd, ">", 3000))
        return false;

    /* 2. 发送裸数据 */
    HAL_UART_Transmit(&huart2, data, len, 1000);

    /* 3. 等待 SEND OK */
    if (!ESP8266_WaitResponse("SEND OK"))
        return false;

    return true;
}

/* ================= 透传模式 ================= */
bool ESP8266_TCP_EnterTransparent(void)
{
    return ESP8266_SendAndWait("AT+CIPMODE=1\r\n", "OK", 1000);
}

void ESP8266_TCP_ExitTransparent(void)
{
    ESP8266_SendString("+++"); // exit transparent mode
}

/* ================= 串口中断接收 ================= */
void ESP8266_RxHandler(uint8_t ch)
{
    if (esp8266_rx_len < ESP8266_RX_MAX - 1) {
        esp8266_rx_buf[esp8266_rx_len++] = ch;
        esp8266_rx_buf[esp8266_rx_len]   = '\0';
    }

    if (ch == '\n') { // 一行接收完成
        esp8266_rx_ok = 1;
    }
}

/* ================= MQTT 简单接口 ================= */
static char mqtt_payload[MQTT_JSON_BUF_LEN];
static bool mqtt_new_msg = false;

void ESP8266_MQTT_Init(void)
{
    memset(mqtt_payload, 0, MQTT_JSON_BUF_LEN);
    mqtt_new_msg = false;
}

bool ESP8266_MQTT_HasMsg(void)
{
    return mqtt_new_msg;
}

char *ESP8266_MQTT_GetPayload(void)
{
    mqtt_new_msg = false;
    return mqtt_payload;
}

/* 处理原始串口数据 -> MQTT payload */
void ESP8266_MQTT_HandleIncomingData(const char *raw)
{
    if (!raw) return;

    if (raw[0] == '{') { // JSON直接上报
        strncpy(mqtt_payload, raw, MQTT_JSON_BUF_LEN - 1);
        mqtt_payload[MQTT_JSON_BUF_LEN - 1] = '\0';
        mqtt_new_msg                        = true;
        return;
    }

    const char *json = strchr(raw, '{'); // AT +MQTTSUB
    if (json) {
        strncpy(mqtt_payload, json, MQTT_JSON_BUF_LEN - 1);
        mqtt_payload[MQTT_JSON_BUF_LEN - 1] = '\0';
        mqtt_new_msg                        = true;
    }
}

/* ================= 直接发送字符串 ================= */
void ESP8266_SendString(const char *str)
{
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t *)str, strlen(str), 1000);
}

bool ESP8266_SendCmd(const char *cmd, const char *ack, uint32_t timeout)
{
    return ESP8266_SendAndWait(cmd, ack, timeout);
}

bool ESP8266_WaitResponse(const char *ack)
{
    return ESP8266_WaitReply(ack, 5000); // 固定超时
}