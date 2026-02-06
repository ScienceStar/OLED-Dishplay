#ifndef __TCP_H
#define __TCP_H

#include <stdint.h>

#define WIFI_SSID   "TP-LINK_F643"   // ?? 2.4G
#define WIFI_PWD    "Vast314&Star159$"

#define TCP_SERVER_IP   "192.168.0.7"      // ??????/???IP
#define TCP_SERVER_PORT 8888                // ????????

extern volatile uint8_t TcpClosedFlag;

void TCP_Task(void);
void TCP_Send_Test(void);
void TCP_Send_Loop(void);
void TCP_Heartbeat(void);
#endif