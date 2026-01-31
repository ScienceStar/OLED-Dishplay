#include "tcp.h"
#include "esp8266.h"

volatile uint8_t TcpClosedFlag = 0;

typedef enum {
    TCP_IDLE,
    TCP_WIFI,
    TCP_CONNECT,
    TCP_WORK
} TCP_STATE;

static TCP_STATE tcp_state = TCP_IDLE;
static uint32_t tcp_retry_count = 0;
static uint32_t tcp_retry_tick = 0;

void TCP_Task(void)
{
    static uint32_t tick = 0;
    uint32_t now = HAL_GetTick();

    if(now - tick < 1000) return;
    tick = now;

    switch(tcp_state)
    {
        case TCP_IDLE:
            if(ESP8266_AT_Test())
            {
                tcp_state = TCP_WIFI;
                tcp_retry_count = 0;
            }
            break;

        case TCP_WIFI:
            if(ESP8266_SetMode(STA) &&
               ESP8266_JoinAP(WIFI_SSID, WIFI_PWD))
            {
                tcp_state = TCP_CONNECT;
                tcp_retry_count = 0;
                tcp_retry_tick = now;
            }
            else
            {
                tcp_retry_count++;
                if(tcp_retry_count > 5)
                {
                    tcp_state = TCP_IDLE;
                    tcp_retry_count = 0;
                }
            }
            break;

        case TCP_CONNECT:
            if(ESP8266_TCP_Connect(TCP_SERVER_IP, TCP_SERVER_PORT))
            {
                tcp_state = TCP_WORK;
                tcp_retry_count = 0;
                tcp_retry_tick = now;
            }
            else
            {
                tcp_retry_count++;
                if(tcp_retry_count > 3)
                {
                    tcp_state = TCP_WIFI;
                    tcp_retry_count = 0;
                }
            }
            break;

        case TCP_WORK:
            // 保持连接，定期检查TCP状态
            // 在main.c中由主循环周期发送数据
            if(TcpClosedFlag || (now - tcp_retry_tick > 30000))
            {
                // 30秒后自动重连
                tcp_state = TCP_IDLE;
                tcp_retry_count = 0;
            }
            break;
    }
}