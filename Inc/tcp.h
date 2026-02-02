#ifndef __TCP_H
#define __TCP_H

#define WIFI_SSID   "TP-LINK_F643"   // ?? 2.4G
#define WIFI_PWD    "3141592654"

#define TCP_SERVER_IP   "192.168.0.7"      // ??????/???IP
#define TCP_SERVER_PORT 8888                // ????????

void TCP_Task(void);
void TCP_Send_Test(void);
void TCP_Send_Loop(void);
void TCP_Heartbeat(void);
#endif