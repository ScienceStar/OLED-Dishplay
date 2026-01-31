#include "esp8266.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

uint8_t  esp8266_rx_buf[ESP8266_RX_MAX];
uint16_t esp8266_rx_len = 0;
volatile uint8_t esp8266_rx_ok = 0;

/* ================= 内部 ================= */
static void esp8266_send(char *cmd)
{
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)cmd, strlen(cmd), 1000);
}

/* ================= 接口实现 ================= */
void ESP8266_Init(void)
{
    HAL_Delay(1000);
}

void ESP8266_ClearRx(void)
{
    memset(esp8266_rx_buf, 0, sizeof(esp8266_rx_buf));
    esp8266_rx_len = 0;
    esp8266_rx_ok  = 0;
}

bool ESP8266_WaitReply(char *ack, uint32_t timeout)
{
    uint32_t tick = HAL_GetTick();
    while(HAL_GetTick() - tick < timeout)
    {
        if(esp8266_rx_ok)
        {
            esp8266_rx_ok = 0;
            if(strstr((char*)esp8266_rx_buf, ack))
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

bool ESP8266_JoinAP(char *ssid, char *pwd)
{
    char cmd[128];
    sprintf(cmd,"AT+CWJAP=\"%s\",\"%s\"\r\n",ssid,pwd);
    ESP8266_ClearRx();
    esp8266_send(cmd);
    return ESP8266_WaitReply("WIFI CONNECTED", 10000);
}

/* ================= TCP ================= */
bool ESP8266_TCP_Connect(char *ip, uint16_t port)
{
    char cmd[64];
    sprintf(cmd,"AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",ip,port);
    ESP8266_ClearRx();
    esp8266_send(cmd);
    return ESP8266_WaitReply("CONNECT", 5000);
}

bool ESP8266_TCP_EnterTransparent(void)
{
    ESP8266_ClearRx();
    esp8266_send("AT+CIPMODE=1\r\n");
    if(!ESP8266_WaitReply("OK",1000)) return false;

    ESP8266_ClearRx();
    esp8266_send("AT+CIPSEND\r\n");
    return ESP8266_WaitReply(">",1000);
}

bool ESP8266_TCP_Send(char *data)
{
    if(data == NULL) return false;
    int len = strlen(data);
    char cmd[32];
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);
    ESP8266_ClearRx();
    esp8266_send(cmd);
    /* 等待 '>' 提示发送 */
    if(!ESP8266_WaitReply(">", 2000)) return false;

    /* 发送数据本体 */
    esp8266_send(data);

    /* 等待发送完成确认 */
    if(ESP8266_WaitReply("SEND OK", 3000)) return true;
    return false;
}

void ESP8266_TCP_ExitTransparent(void)
{
    HAL_Delay(500);
    esp8266_send("+++");
    HAL_Delay(500);
}

void ESP8266_SendString(const char *str)
{
    while(*str)
    {
        HAL_UART_Transmit(&ESP8266_UART, (uint8_t*)str, 1, 100);
        str++;
    }
}