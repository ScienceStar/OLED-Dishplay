#ifndef __ESP8266_WIFI_ADAPTER_H
#define __ESP8266_WIFI_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

bool WiFi_begin(const char *ssid, const char *pwd);
uint8_t WiFi_connected(void);
int8_t WiFi_RSSI(void);

bool TCP_begin(const char *ip, uint16_t port);
bool TCP_send_str(const char *data);
void TCP_end(void);

#endif
