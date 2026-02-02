#include "esp8266_wifi_adapter.h"
#include "esp8266.h"
#include <string.h>

bool WiFi_begin(const char *ssid, const char *pwd)
{
    ESP8266_ClearRx();
    ESP8266_Init();
    if(!ESP8266_AT_Test()) return false;
    if(!ESP8266_SetMode(STA)) return false;
    return ESP8266_JoinAP((char*)ssid, (char*)pwd);
}

uint8_t WiFi_connected(void)
{
    // rely on global WiFiStatus if available, otherwise test
    extern volatile uint8_t WiFiStatus;
    return WiFiStatus;
}

int8_t WiFi_RSSI(void)
{
    extern int8_t WiFiRSSI;
    return WiFiRSSI;
}

bool TCP_begin(const char *ip, uint16_t port)
{
    return ESP8266_TCP_Connect((char*)ip, port);
}

bool TCP_send_str(const char *data)
{
    return ESP8266_TCP_Send((char*)data);
}

void TCP_end(void)
{
    ESP8266_TCP_ExitTransparent();
}
