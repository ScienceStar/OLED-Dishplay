#include "esp8266.h"
#include "usart.h"
#include "tcp.h"
#include "cabinet_view.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ================== 缓存定义 ================== */
#define ESP8266_RX_MAX 1024
#define MQTT_PAYLOAD_MAX 128
uint8_t esp8266_rx_buf[ESP8266_RX_MAX];
uint16_t esp8266_rx_len        = 0;
volatile uint8_t esp8266_rx_ok = 0;
static char mqtt_payload[MQTT_PAYLOAD_MAX];
static volatile bool mqtt_msg_flag = false;

/* ================== 内部函数 ================== */
static void esp8266_send(const char *cmd)
{
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t *)cmd, strlen(cmd), 1000);
}

/* ================== 公共函数 ================== */
void ESP8266_ClearRx(void)
{
    memset(esp8266_rx_buf, 0, sizeof(esp8266_rx_buf));
    esp8266_rx_len = 0;
    esp8266_rx_ok  = 0;
}

bool ESP8266_WaitReply(const char *ack, uint32_t timeout)
{
    uint32_t tick = HAL_GetTick();
    while (HAL_GetTick() - tick < timeout) {
        if (esp8266_rx_ok) {
            esp8266_rx_ok = 0;
            if (strstr((char *)esp8266_rx_buf, ack))
                return true;
        }
    }
    return false;
}

bool ESP8266_AT_Test(void)
{
    ESP8266_ClearRx();
    esp8266_send("AT\r\n");
    return ESP8266_WaitReply("OK", 1000);
}

bool ESP8266_SetMode(ESP8266_Mode mode)
{
    char cmd[32];
    sprintf(cmd, "AT+CWMODE=%d\r\n", mode);
    ESP8266_ClearRx();
    esp8266_send(cmd);
    return ESP8266_WaitReply("OK", 2000);
}

bool ESP8266_JoinAP(const char *ssid, const char *pwd)
{
    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    ESP8266_ClearRx();
    esp8266_send(cmd);

    // 兼容不同固件返回 OK 或 WIFI CONNECTED
    return ESP8266_WaitReply("WIFI CONNECTED", 10000) || ESP8266_WaitReply("OK", 10000);
}

bool ESP8266_TCP_Connect(const char *ip, uint16_t port)
{
    char cmd[80];
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ip, port);
    ESP8266_ClearRx();
    esp8266_send(cmd);

    return ESP8266_WaitReply("CONNECT", 5000) ||
           ESP8266_WaitReply("ALREADY CONNECTED", 2000) ||
           ESP8266_WaitReply("OK", 2000);
}

bool ESP8266_TCP_Send(const char *data)
{
    if (!data) return false;
    int len = strlen(data);
    char cmd[32];
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);
    ESP8266_ClearRx();
    esp8266_send(cmd);

    if (!ESP8266_WaitReply(">", 2000)) return false;

    esp8266_send(data);

    return ESP8266_WaitReply("SEND OK", 3000);
}

bool ESP8266_TCP_EnterTransparent(void)
{
    ESP8266_ClearRx();
    esp8266_send("AT+CIPMODE=1\r\n");
    if (!ESP8266_WaitReply("OK", 1000))
        return false; // 设置透传模式失败

    ESP8266_ClearRx();
    esp8266_send("AT+CIPSEND\r\n");
    if (!ESP8266_WaitReply(">", 1000))
        return false; // 进入发送状态失败

    return true; // 成功
}

void ESP8266_TCP_ExitTransparent(void)
{
    HAL_Delay(500);
    esp8266_send("+++");
    HAL_Delay(500);
}

void ESP8266_SendString(const char *str)
{
    while (*str) {
        HAL_UART_Transmit(&ESP8266_UART, (uint8_t *)str, 1, 100);
        str++;
    }
}

/* ================== 初始化 ================== */
void ESP8266_Init(void)
{
    ESP8266_ClearRx();
    HAL_Delay(1000);
}

/* ================== MQTT 初始化 ================== */
bool ESP8266_SendAndWait(const char *cmd, const char *ack, uint32_t timeout)
{
    uint32_t tickstart = HAL_GetTick();
    ESP8266_ClearRx();

    HAL_UART_Transmit(&ESP8266_UART,
                      (uint8_t *)cmd,
                      strlen(cmd),
                      1000);

    while ((HAL_GetTick() - tickstart) < timeout) {
        if (strstr((char *)esp8266_rx_buf, ack)) {
            return true;
        }
    }
    return false;
}

void ESP8266_MQTT_Init(void)
{
    char cmd[128];

    ESP8266_SendAndWait("AT\r\n", "OK", 1000);
    ESP8266_SendAndWait("AT+RST\r\n", "ready", 5000);
    ESP8266_SendAndWait("AT+CWMODE=1\r\n", "OK", 1000);

    snprintf(cmd, sizeof(cmd),
             "AT+CWJAP=\"%s\",\"%s\"\r\n",
             WIFI_SSID, WIFI_PWD);

    if (!ESP8266_SendAndWait(cmd, "WIFI GOT IP", 15000)) return;

    ESP8266_SendAndWait(
        "AT+MQTTUSERCFG=0,1,\"cabinet\",\"\",\"\",0,0,\"\"\r\n",
        "OK",
        1000);

    ESP8266_SendAndWait(
        "AT+MQTTCONN=0,\"broker.hivemq.com\",1883,0\r\n",
        "+MQTTCONNECTED",
        5000);

    ESP8266_SendAndWait(
        "AT+MQTTSUB=0,\"cabinet/status\",1\r\n",
        "OK",
        1000);
}

// 检查是否有新消息
bool ESP8266_MQTT_HasMsg(void)
{
    return mqtt_msg_flag;
}

// 获取消息内容，并清除标志
char* ESP8266_MQTT_GetPayload(void)
{
    if (!mqtt_msg_flag) return NULL;

    mqtt_msg_flag = false;
    return mqtt_payload;
}

/* ================== 接收处理 ================== */
void ESP8266_RxHandler(uint8_t ch)
{
    if (esp8266_rx_len < ESP8266_RX_MAX - 1) {
        esp8266_rx_buf[esp8266_rx_len++] = ch;
    }

    if (ch == '\n') {
        esp8266_rx_buf[esp8266_rx_len] = 0;

        if (strstr((char *)esp8266_rx_buf, "+MQTTSUBRECV")) {
            char *json = strchr((char *)esp8266_rx_buf, '{');
            if (json) {
                CabinetView_UpdateFromJson(json);
            }
        }

        esp8266_rx_len = 0;
        memset(esp8266_rx_buf, 0, sizeof(esp8266_rx_buf));
        esp8266_rx_ok = 1;
    }
}