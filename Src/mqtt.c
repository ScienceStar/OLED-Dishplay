#include "mqtt.h"
#include <string.h>
#include <stdio.h>

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

/* ================= 连接（逻辑） ================= */
bool MQTT_Connect(MQTT_Client *client)
{
    if (!client) return false;

    /*
     * ⚠️ 注意：
     * STM32 并不真正实现 MQTT 协议
     * 这里只表示：
     *   - ESP8266 已上电
     *   - 串口通信正常
     *   - 可以开始接收 MQTT PUBLISH 数据
     */
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
    client->new_msg = true;
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
        client->new_msg = true;
        return;
    }

    /* 情况 2：AT 返回，截取 payload */
    const char *json = strchr(raw, '{');
    if (json) {
        strncpy(client->json_buf, json, MQTT_JSON_BUF_LEN - 1);
        client->json_buf[MQTT_JSON_BUF_LEN - 1] = '\0';
        client->new_msg = true;
    }
}