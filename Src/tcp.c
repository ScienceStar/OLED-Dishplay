#include "tcp.h"
#include "esp8266.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

volatile uint8_t TcpClosedFlag = 0;

typedef enum
{
    TCP_IDLE = 0,
    TCP_WIFI,
    TCP_CONNECT,
    TCP_WORK
} TCP_STATE;

static TCP_STATE tcp_state = TCP_IDLE;
static uint8_t retry_count = 0;
static uint32_t last_tick = 0;

/* 循环发送功能 */
static uint32_t last_send_tick = 0;
#define TCP_SEND_INTERVAL 1000 // 每1000ms发送一次
static uint32_t last_heartbeat_tick = 0;

/* ================= 定时器 ================= */
static uint32_t last_at_tick      = 0;
static uint32_t last_wifi_tick    = 0;
static uint32_t last_connect_tick = 0;

void TCP_Task(void)
{
    uint32_t now = HAL_GetTick();

    switch (tcp_state)
    {
    case TCP_IDLE:
        if (now - last_at_tick >= 500) { // AT测试每500ms
            last_at_tick = now;
            if (ESP8266_AT_Test()) {
                tcp_state = TCP_WIFI;
                retry_count = 0;
            }
        }
        break;

    case TCP_WIFI:
        if (now - last_wifi_tick >= 500) { // 进入模式/连接AP每500ms
            last_wifi_tick = now;
            if (ESP8266_SetMode(STA)) {
                if (ESP8266_JoinAP(WIFI_SSID, WIFI_PWD)) {
                    tcp_state = TCP_CONNECT;
                    retry_count = 0;
                }
            } else if (++retry_count >= 3) {
                tcp_state = TCP_IDLE;
                retry_count = 0;
            }
        }
        break;

    case TCP_CONNECT:
        if (now - last_connect_tick >= 500) { // TCP连接尝试每500ms
            last_connect_tick = now;
            if (ESP8266_TCP_Connect(TCP_SERVER_IP, TCP_SERVER_PORT)) {
                tcp_state = TCP_WORK;
                TcpClosedFlag = 0;
                retry_count = 0;
            } else if (++retry_count >= 3) {
                tcp_state = TCP_WIFI;
                retry_count = 0;
            }
        }
        break;

    case TCP_WORK:
        if (TcpClosedFlag) {
            tcp_state = TCP_IDLE;
            TcpClosedFlag = 0;
        }
        break;
    }
}

void TCP_Send_Test(void)
{
    if (tcp_state == TCP_WORK)
    {
        ESP8266_TCP_Send("Hello from STM32\r\n");
    }
}

/* ================= 循环发送功能 ================= */
void TCP_Send_Loop(void)
{
    if (tcp_state != TCP_WORK)
        return; // 未连接则返回

    uint32_t now = HAL_GetTick();
    if (now - last_send_tick >= TCP_SEND_INTERVAL)
    {
        ESP8266_TCP_Send("Loop message from STM32\r\n");
        last_send_tick = now;
    }
}

/* ========== 心跳 ========== */
void TCP_Heartbeat(void)
{
    uint32_t now = HAL_GetTick();
    if(tcp_state != TCP_WORK) return;

    if(now - last_heartbeat_tick >= 2000) // 每2秒心跳
    {
        ESP8266_TCP_Send("PING\r\n");
        last_heartbeat_tick = now;
    }
}