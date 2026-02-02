#include "mqtt.h"
#include <string.h>
#include <stdio.h>
#include "esp8266.h"

/* ================= 内部定义 ================= */
#define MQTT_MSG_QUEUE_SIZE  5

static char msg_queue[MQTT_MSG_QUEUE_SIZE][MQTT_JSON_BUF_LEN];
static uint8_t msg_head = 0;
static uint8_t msg_tail = 0;
static uint8_t msg_count = 0;

/* ================= 内部工具 ================= */
static void mqtt_enqueue(const char *json)
{
    if (msg_count >= MQTT_MSG_QUEUE_SIZE) {
        // 队列满，丢弃最旧一条
        msg_head = (msg_head + 1) % MQTT_MSG_QUEUE_SIZE;
        msg_count--;
    }

    strncpy(msg_queue[msg_tail], json, MQTT_JSON_BUF_LEN - 1);
    msg_queue[msg_tail][MQTT_JSON_BUF_LEN - 1] = '\0';

    msg_tail = (msg_tail + 1) % MQTT_MSG_QUEUE_SIZE;
    msg_count++;
}

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

    msg_head = msg_tail = msg_count = 0;
}

/* ================= 连接 ================= */
bool MQTT_Connect(MQTT_Client *client)
{
    if (!client) return false;

    char cmd[128];

    sprintf(cmd,
            "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",
            client->broker,
            client->port);

    ESP8266_SendCmd(cmd, "OK", 5000);
    if (!ESP8266_WaitResponse("CONNECT")) {
        return false;
    }

    MQTT_SendConnectPacket(client);

    if (!ESP8266_WaitResponse("CONNACK")) {
        return false;
    }

    client->connected = true;
    return true;
}

/* ================= 订阅 ================= */
bool MQTT_Subscribe(MQTT_Client *client)
{
    if (!client || !client->connected) return false;

    MQTT_SendSubscribePacket(client, client->topic);

    if (!ESP8266_WaitResponse("SUBACK")) {
        return false;
    }

    return true;
}

/* ================= 检查是否有新消息 ================= */
bool MQTT_MessageReceived(MQTT_Client *client)
{
    if (!client) return false;

    if (msg_count == 0) return false;

    strncpy(client->json_buf,
            msg_queue[msg_head],
            MQTT_JSON_BUF_LEN - 1);
    client->json_buf[MQTT_JSON_BUF_LEN - 1] = '\0';

    msg_head = (msg_head + 1) % MQTT_MSG_QUEUE_SIZE;
    msg_count--;

    return true;
}

/* ================= 处理 ESP8266 原始数据 ================= */
void MQTT_HandleIncomingData(MQTT_Client *client, const char *raw)
{
    if (!client || !raw) return;

    const char *json = strchr(raw, '{');
    if (!json) return;

    /* 简单校验：必须包含 } */
    if (!strchr(json, '}')) return;

    mqtt_enqueue(json);
}

void MQTT_SendConnectPacket(MQTT_Client *client) {
    char packet[128];
    snprintf(packet, sizeof(packet),
             "CONNECT Packet for %s", client->client_id);
    ESP8266_SendCmd(packet, "OK", 1000);
}

void MQTT_SendSubscribePacket(MQTT_Client *client, const char *topic) {
    char packet[128];
    snprintf(packet, sizeof(packet),
             "SUBSCRIBE Packet to %s", topic);
    ESP8266_SendCmd(packet, "OK", 1000);
}

/* ================= 测试用 ================= */
void MQTT_SimulateIncomingMessage(MQTT_Client *client, const char *json)
{
    if (!client || !json) return;
    mqtt_enqueue(json);
}