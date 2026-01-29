#ifndef __ESP8266_H
#define __ESP8266_H 			   
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

#if defined ( __CC_ARM )
#pragma anon_unions
#endif

/* ESP8266模式选择 */
typedef enum { STA, AP, STA_AP } ENUM_Net_ModeTypeDef;

/* 网络传输层协议 */
typedef enum { enumTCP, enumUDP } ENUM_NetPro_TypeDef;

/* 连接号 */
typedef enum {
    Multiple_ID_0=0, Multiple_ID_1, Multiple_ID_2,
    Multiple_ID_3, Multiple_ID_4, Single_ID_0=5
} ENUM_ID_NO_TypeDef;

/* ESP8266 GPIO配置 */
#define ESP8266_RST_Pin                 GPIO_PIN_13
#define ESP8266_RST_Pin_Port            GPIOC
#define ESP8266_RST_Pin_Periph_Clock()  __HAL_RCC_GPIOC_CLK_ENABLE()
#define ESP8266_RST_Pin_SetH             HAL_GPIO_WritePin(ESP8266_RST_Pin_Port,ESP8266_RST_Pin,GPIO_PIN_SET)
#define ESP8266_RST_Pin_SetL             HAL_GPIO_WritePin(ESP8266_RST_Pin_Port,ESP8266_RST_Pin,GPIO_PIN_RESET)

/* 串口打印 */
#define ESP8266_USART(fmt, ...)  USART_printf(USART2, fmt, ##__VA_ARGS__)
#define PC_USART(fmt, ...)       printf(fmt, ##__VA_ARGS__)

/* 数据帧结构 */
#define RX_BUF_MAX_LEN 1024
extern struct STRUCT_USART_Fram {
    char Data_RX_BUF[RX_BUF_MAX_LEN];
    union {
        __IO u16 InfAll;
        struct {
            __IO u16 FramLength       :15;
            __IO u16 FramFinishFlag   :1;
        }InfBit;
    };
} ESP8266_Fram_Record_Struct;

/* ================= ESP8266功能函数 ================= */
void ESP8266_Init(void);
void ESP8266_AT_Test(void);
bool ESP8266_Send_AT_Cmd(char *cmd,char *ack1,char *ack2,u32 time);
void ESP8266_Rst(void);
bool ESP8266_Net_Mode_Choose(ENUM_Net_ModeTypeDef enumMode);
bool ESP8266_JoinAP(char *pSSID, char *pPassWord);
bool ESP8266_Enable_MultipleId(FunctionalState enumEnUnvarnishTx);
bool ESP8266_Link_Server(ENUM_NetPro_TypeDef enumE, char *ip, char *ComNum, ENUM_ID_NO_TypeDef id);
bool ESP8266_SendString(FunctionalState enumEnUnvarnishTx, char *pStr, u32 ulStrLength, ENUM_ID_NO_TypeDef ucId);
bool ESP8266_UnvarnishSend(void);
void ESP8266_ExitUnvarnishSend(void);
u8 ESP8266_Get_LinkStatus(void);
void USART_printf(USART_TypeDef * USARTx, char * Data, ...);

/* ================= MQTT功能 ================= */
bool ESP8266_MQTTUSERCFG(char *pClient_Id, char *pUserName,char *PassWord);
bool ESP8266_MQTTCONN(char *Ip, int Num);
bool ESP8266_MQTTSUB(char *Topic);
bool ESP8266_MQTTPUB(char *Topic,char *temp);
bool ESP8266_MQTTCLEAN(void);
bool MQTT_SendString(char *pTopic,char *temp2);

#endif