#ifndef __MQTT_H
#define __MQTT_H

#include <stdint.h>
#include <stdbool.h>

// 使用本地EMQX Broker
#define MQTT_BROKER_HOST "192.168.0.6"
#define MQTT_BROKER_PORT 1883  // EMQX默认端口是1883，不是1896

#define MQTT_JSON_BUF_LEN 128
#define MQTT_BROKER_MAXLEN 64
#define MQTT_CLIENTID_MAXLEN 32
#define MQTT_TOPIC_MAXLEN 64

/* SUBSCRIBE 报文固定 Packet ID，这里用 1 */
#define MQTT_SUB_PACKET_ID 0x0001

typedef struct {
    char broker[MQTT_BROKER_MAXLEN];
    uint16_t port;
    char client_id[MQTT_CLIENTID_MAXLEN];
    char topic[MQTT_TOPIC_MAXLEN];
    bool connected;
    char json_buf[MQTT_JSON_BUF_LEN]; // 存储最新消息
    bool new_msg;                     // 是否有新消息
} MQTT_Client;

/* ================= MQTT 接口 ================= */

/**
 * 初始化 MQTT 客户端
 */
bool MQTT_Init(MQTT_Client *client, const char *broker, uint16_t port,
               const char *client_id, const char *topic);

/**
 * 连接 MQTT Broker
 */
bool MQTT_Connect(MQTT_Client *client);

void MQTT_Yield(MQTT_Client *client, uint32_t timeout_ms);

/**
 * 订阅指定 topic
 */
bool MQTT_Subscribe(MQTT_Client *client);

/**
 * 非阻塞检查是否有新消息
 */
bool MQTT_MessageReceived(MQTT_Client *client);

/**
 * 模拟接收消息（测试用，可替换为 ESP8266 实际处理）
 */
void MQTT_SimulateIncomingMessage(MQTT_Client *client, const char *json);

/**
 * 处理 ESP8266 接收到的原始字符串，将 topic / payload 填入 client->json_buf
 * 这里可以根据实际 AT 返回解析
 */
void MQTT_HandleIncomingData(MQTT_Client *client, const char *raw);

/**
 * 等待 CONNACK 报文
 */
bool MQTT_Wait_CONNACK(uint32_t timeout_ms);

/**
 * 直接发送MQTT CONNECT包（不重新连接WiFi/TCP）
 */
bool MQTT_SendConnectPacket(void);

/**
 * 建立到MQTT Broker的TCP连接
 */
bool MQTT_ConnectToBroker(void);

/**
 * 完整的MQTT连接流程：TCP连接 + MQTT CONNECT
 */
bool MQTT_FullConnect(MQTT_Client *client);

#endif