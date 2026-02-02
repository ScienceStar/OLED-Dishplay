#include "mqtt.h"
#include <string.h>
#include <stdio.h>

/* ================= MQTT 实现 ================= */
void MQTT_Init(MQTT_Client *client, const char *broker, uint16_t port,
               const char *client_id, const char *topic)
{
    strncpy(client->broker, broker, MQTT_BROKER_MAXLEN-1);
    client->broker[MQTT_BROKER_MAXLEN-1] = '\0';
    client->port = port;

    strncpy(client->client_id, client_id, MQTT_CLIENTID_MAXLEN-1);
    client->client_id[MQTT_CLIENTID_MAXLEN-1] = '\0';

    strncpy(client->topic, topic, MQTT_TOPIC_MAXLEN-1);
    client->topic[MQTT_TOPIC_MAXLEN-1] = '\0';

    client->connected = false;
    client->new_msg = false;
    memset(client->json_buf, 0, MQTT_JSON_BUF_LEN);
}

/* ------------------ 连接 ------------------ */
bool MQTT_Connect(MQTT_Client *client)
{
    // TODO: 替换成 ESP8266 AT 指令实现真实连接
    // 示例：
    // AT+CIPSTART="TCP","broker",1883
    client->connected = true;
    return client->connected;
}

/* ------------------ 订阅 ------------------ */
bool MQTT_Subscribe(MQTT_Client *client)
{
    if (!client->connected) return false;

    // TODO: 替换成 ESP8266 AT 指令订阅 topic
    // 示例：
    // AT+MQTTSUB="topic",qos
    return true;
}

/* ------------------ 检查新消息 ------------------ */
bool MQTT_MessageReceived(MQTT_Client *client)
{
    if(client->new_msg) {
        client->new_msg = false;
        return true;
    }
    return false;
}

/* ------------------ 模拟消息 ------------------ */
void MQTT_SimulateIncomingMessage(MQTT_Client *client, const char *json)
{
    strncpy(client->json_buf, json, MQTT_JSON_BUF_LEN-1);
    client->json_buf[MQTT_JSON_BUF_LEN-1] = '\0';
    client->new_msg = true;
}

/* ------------------ 处理 ESP8266 原始数据 ------------------ */
void MQTT_HandleIncomingData(MQTT_Client *client, const char *raw)
{
    // 简单示例：假设 raw 格式为 "+MQTTSUB:<topic>,<payload>"
    // 解析 payload 到 json_buf
    const char *p = strchr(raw, ',');
    if(p) {
        p++; // 跳过逗号
        strncpy(client->json_buf, p, MQTT_JSON_BUF_LEN-1);
        client->json_buf[MQTT_JSON_BUF_LEN-1] = '\0';
        client->new_msg = true;
    }
}