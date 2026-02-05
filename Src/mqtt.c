#include "mqtt.h"
#include "tcp.h"
#include <string.h>
#include <stdio.h>
#include "esp8266.h"

/* ================== MQTT 客户端 ================== */
MQTT_Client mqttClient                = {0}; // 全部字段初始化为 0

/* ================= 初始化 ================= */
bool MQTT_Init(MQTT_Client *client,
               const char *broker,
               uint16_t port,
               const char *client_id,
               const char *topic)
{
    if (!client || !broker || !client_id || !topic)
        return false;

    /* broker */
    if (strlen(broker) >= MQTT_BROKER_MAXLEN)
        return false;
    strcpy(client->broker, broker);

    /* port */
    if (port == 0 || port > 65535)
        return false;
    client->port = port;

    /* client id */
    if (strlen(client_id) >= MQTT_CLIENTID_MAXLEN)
        return false;
    strcpy(client->client_id, client_id);

    /* topic */
    if (strlen(topic) >= MQTT_TOPIC_MAXLEN)
        return false;
    strcpy(client->topic, topic);

    /* state init */
    client->connected = false;
    client->new_msg   = false;

    memset(client->json_buf, 0, MQTT_JSON_BUF_LEN);

    return true;
}

static const uint8_t mqtt_connect_pkt[] = {
    0x10, 0x12, // MQTT Control Packet type = CONNECT
    0x00, 0x04, 'M', 'Q', 'T', 'T',
    0x04,                          // MQTT version 3.1.1
    0x02,                          // Clean Session
    0x00, 0x3C,                    // KeepAlive = 60s
    0x00, 0x04, 't', 'e', 's', 't' // ClientID = "test"
};

/* ================= 连接（逻辑） ================= */
bool MQTT_Connect(MQTT_Client *client)
{
    if (!client) return false;

    /* 1. WiFi */
    if (!ESP8266_JoinAP(WIFI_SSID, WIFI_PWD))
        return false;

    /* 2. TCP */
    if (!ESP8266_TCP_Connect(MQTT_BROKER_HOST,
                             MQTT_BROKER_PORT))
        return false;

    /* 3. 清空缓冲 */
    ESP8266_ClearRx(); // ← 用你现有函数名

    /* 4. 发送 CONNECT */
    if (!ESP8266_SendRaw((uint8_t *)mqtt_connect_pkt,
                         sizeof(mqtt_connect_pkt)))
        return false;

    /* 5. 等 CONNACK */
    if (!MQTT_Wait_CONNACK(3000)) {
        ESP8266_TCP_Close();
        return false;
    }

    client->connected = true;
    return true;
}

/**
 * @brief 轻量级 MQTT 循环处理函数
 * @param client MQTT 客户端结构体
 * @param timeout_ms 超时时间（毫秒）
 * @note 这个函数用于代替 MQTT_Yield
 */
void MQTT_Yield(MQTT_Client *client, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (HAL_GetTick() - start < timeout_ms) {
        // 如果ESP8266有接收到数据
        if (esp8266_rx_len > 0) {
            // 拷贝数据到本地缓冲
            uint16_t len = esp8266_rx_len;
            if (len > 127) len = 127; // 限制缓冲
            memcpy(client->json_buf, esp8266_rx_buf, len);
            client->json_buf[len] = '\0';
            esp8266_rx_len        = 0;

            // 标记有新消息
            client->new_msg = true;
        }

        // 这里不做心跳，保持最简单
    }
}

/* ================= 订阅 ================= */
bool MQTT_Subscribe(MQTT_Client *client)
{
    if (!client || !client->connected)
        return false;

    uint8_t sub_pkt[128]; // 根据 topic 长度动态调整即可
    uint16_t len       = 0;
    uint16_t topic_len = strlen(client->topic);

    if (topic_len + 5 > sizeof(sub_pkt))
        return false; // topic 太长

    /* 固定报文头：SUBSCRIBE, QoS 1 */
    sub_pkt[len++] = 0x82;          // SUBSCRIBE, QoS=1
    sub_pkt[len++] = topic_len + 3; // 剩余长度 = PacketID(2) + topic_len + QoS(1)

    /* Packet ID */
    sub_pkt[len++] = (MQTT_SUB_PACKET_ID >> 8) & 0xFF;
    sub_pkt[len++] = MQTT_SUB_PACKET_ID & 0xFF;

    /* Topic */
    sub_pkt[len++] = (topic_len >> 8) & 0xFF;
    sub_pkt[len++] = topic_len & 0xFF;
    memcpy(&sub_pkt[len], client->topic, topic_len);
    len += topic_len;

    /* QoS */
    sub_pkt[len++] = 0x00; // QoS 0

    /* 发送报文 */
    if (!ESP8266_SendRaw(sub_pkt, len))
        return false;

    /* 等待 SUBACK 响应（简化版，实际可解析二进制） */
    // 如果你还没实现二进制解析，先直接返回 true
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