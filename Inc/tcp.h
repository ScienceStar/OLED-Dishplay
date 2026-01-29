#ifndef __TCP_H
#define __TCP_H 			   

#include "main.h"

/* ================= 用户配置 ================= */
#define User_ESP8266_SSID     "iPhone"          
#define User_ESP8266_PWD      "Vast314&Star159$"
#define User_ESP8266_TCPServer_IP     "192.168.0.6"
#define User_ESP8266_TCPServer_PORT   "8888"

/* ================= 全局变量 ================= */
extern volatile uint8_t TcpClosedFlag;  //连接状态标志

/* ================= 函数声明 ================= */
void ESP8266_STA_TCPClient_Test(void);

// 添加这一行，声明 TCP_ProcessData
void TCP_ProcessData(uint8_t *data, uint16_t len);

#endif