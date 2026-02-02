#ifndef __MQTT_H
#define __MQTT_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#define MQTT_JSON_BUF_LEN     128
#define MQTT_BROKER_MAXLEN    64
#define MQTT_CLIENTID_MAXLEN  32
#define MQTT_TOPIC_MAXLEN     64

typedef struct {
    /* ===== 配置参数（仅保存，不代表 STM32 真正连 MQTT） ===== */
    char broker[MQTT_BROKER_MAXLEN];
    uint16_t port;
    char client_id[MQTT_CLIENTID_MAXLEN];
    char topic[MQTT_TOPIC_MAXLEN];

    /* ===== 状态 ===== */
    bool connected;                 // 仅表示 ESP8266 已准备好（非真实 MQTT 状态）

    /* ===== 接收缓存 ===== */
    char json_buf[MQTT_JSON_BUF_LEN]; // 存储最新一条 JSON
    bool new_msg;                     // 是否收到新消息
} MQTT_Client;

/* ================= MQTT 接口 ================= */

/**
 * 初始化 MQTT 客户端结构体
 */
void MQTT_Init(MQTT_Client *client,
               const char *broker,
               uint16_t port,
               const char *client_id,
               const char *topic);

/**
 * 触发 ESP8266 建立连接（逻辑连接）
 */
bool MQTT_Connect(MQTT_Client *client);

/**
 * 触发 ESP8266 订阅 Topic（逻辑订阅）
 */
bool MQTT_Subscribe(MQTT_Client *client);

/**
 * 非阻塞：检查是否收到新 MQTT 消息
 */
bool MQTT_MessageReceived(MQTT_Client *client);

/**
 * 模拟接收消息（调试 / 单元测试用）
 */
void MQTT_SimulateIncomingMessage(MQTT_Client *client, const char *json);

/**
 * 处理 ESP8266 串口收到的原始数据
 * 要求：最终 payload 为 JSON 字符串
 */
void MQTT_HandleIncomingData(MQTT_Client *client, const char *raw);

/* ===== MQTT 报文发送函数 ===== */
void MQTT_SendConnectPacket(MQTT_Client *client);
void MQTT_SendSubscribePacket(MQTT_Client *client, const char *topic);
#endif /* __MQTT_H */