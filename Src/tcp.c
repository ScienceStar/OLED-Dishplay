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

void TCP_Task(void)
{
    static uint32_t tick = 0;

    if(HAL_GetTick() - tick < 1000) return;
    tick = HAL_GetTick();

    switch(tcp_state)
    {
        case TCP_IDLE:
            if(ESP8266_AT_Test())
                tcp_state = TCP_WIFI;
            break;

        case TCP_WIFI:
            if(ESP8266_SetMode(STA) &&
               ESP8266_JoinAP(WIFI_SSID, WIFI_PWD))
                tcp_state = TCP_CONNECT;
            break;

        case TCP_CONNECT:
            if(ESP8266_TCP_Connect(TCP_SERVER_IP, TCP_SERVER_PORT) &&
               ESP8266_TCP_EnterTransparent())
                tcp_state = TCP_WORK;
            break;

        case TCP_WORK:
            ESP8266_TCP_Send("杭州光子物联科技有限公司\r\n");
            break;
    }
}