#include "mqtt.h"
#include <string.h>
#include <stdio.h>
#include "esp8266.h"

/* ================= 初始化 ================= */
void MQTT_Init(MQTT_Client *client,
               const char *broker,
               uint16_t port,
               const char *client_id,
               const char *topic)
{
    if (!client) return;

    strncpy(client->broker, broker, MQTT_BROKER_MAXLEN - 1);
    client->broker[MQTT_BROKER_MAXLEN - 1] = '\0';

    client->port = port;

    strncpy(client->client_id, client_id, MQTT_CLIENTID_MAXLEN - 1);
    client->client_id[MQTT_CLIENTID_MAXLEN - 1] = '\0';

    strncpy(client->topic, topic, MQTT_TOPIC_MAXLEN - 1);
    client->topic[MQTT_TOPIC_MAXLEN - 1] = '\0';

    client->connected = false;
    client->new_msg   = false;
    memset(client->json_buf, 0, MQTT_JSON_BUF_LEN);
}

static const uint8_t mqtt_connect_pkt[] = {
    0x10, 0x12, // MQTT Control Packet type = CONNECT
    0x00, 0x04, 'M', 'Q', 'T', 'T',
    0x04,                          // MQTT version 3.1.1
    0x02,                          // Clean Session
    0x00, 0x3C,                    // KeepAlive = 60s
    0x00, 0x04, 't', 'e', 's', 't' // ClientID = "test"
};

#define MQTT_BROKER_HOST "192.168.0.6"
#define MQTT_BROKER_PORT 1883
/* ================= 连接（逻辑） ================= */
bool MQTT_Connect(MQTT_Client *client)
{
    if (!client) return false;

    /* 1. TCP 连接 */
    if (!ESP8266_TCP_Connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT))
        return false;

    /* 2. 发送 MQTT CONNECT 报文 */
    if (!ESP8266_SendRaw((uint8_t *)mqtt_connect_pkt,
                         sizeof(mqtt_connect_pkt))) {
        return false;
    }

    /* 3. 等待 CONNACK */
    if (!MQTT_Wait_CONNACK(3000))
        return false;

    client->connected = true;
    return true;
}

/* ================= 订阅（逻辑） ================= */
bool MQTT_Subscribe(MQTT_Client *client)
{
    if (!client || !client->connected)
        return false;

    /*
     * 实际订阅行为应由 ESP8266 AT 指令完成
     * STM32 只保存 topic，用于调试或日志
     */
    return true;
}

/* ================= 检查新消息 ================= */
bool MQTT_MessageReceived(MQTT_Client *client)
{
    if (!client) return false;

    if (client->new_msg) {
        client->new_msg = false;
        return true;
    }
    return false;
}

/* ================= 模拟消息 ================= */
void MQTT_SimulateIncomingMessage(MQTT_Client *client, const char *json)
{
    if (!client || !json) return;

    strncpy(client->json_buf, json, MQTT_JSON_BUF_LEN - 1);
    client->json_buf[MQTT_JSON_BUF_LEN - 1] = '\0';
    client->new_msg                         = true;
}

/* ================= 处理 ESP8266 原始串口数据 ================= */
/*
 * 支持以下格式：
 * 1) {"cell":"01","open":1,"err":0}
 * 2) +MQTTSUB:"topic",{"cell":"01","open":1,"err":0}
 */
void MQTT_HandleIncomingData(MQTT_Client *client, const char *raw)
{
    if (!client || !raw) return;

    /* 情况 1：ESP8266 直接吐 JSON */
    if (raw[0] == '{') {
        strncpy(client->json_buf, raw, MQTT_JSON_BUF_LEN - 1);
        client->json_buf[MQTT_JSON_BUF_LEN - 1] = '\0';
        client->new_msg                         = true;
        return;
    }

    /* 情况 2：AT 返回，截取 payload */
    const char *json = strchr(raw, '{');
    if (json) {
        strncpy(client->json_buf, json, MQTT_JSON_BUF_LEN - 1);
        client->json_buf[MQTT_JSON_BUF_LEN - 1] = '\0';
        client->new_msg                         = true;
    }
}

bool MQTT_Wait_CONNACK(uint32_t timeout_ms)
{
    uint32_t tick = HAL_GetTick();

    while (HAL_GetTick() - tick < timeout_ms) {
        if (esp8266_rx_len >= 4) {
            uint8_t *buf = esp8266_rx_buf;

            // CONNACK: 20 02 00 00
            if (buf[0] == 0x20 && buf[1] == 0x02 && buf[3] == 0x00)
                return true;
        }
    }
    return false;
}