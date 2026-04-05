#include "mqtt.h"
#include "tcp.h"
#include <string.h>
#include <stdio.h>
#include "esp8266.h"
#include "usart.h"

/* ================== MQTT 客户端 ================== */
MQTT_Client mqttClient = {0};

/* ================= 初始化 ================= */
bool MQTT_Init(MQTT_Client *client,
               const char *broker,
               uint16_t port,
               const char *client_id,
               const char *topic)
{
    if (!client || !broker || !client_id || !topic)
        return false;

    if (strlen(broker) >= MQTT_BROKER_MAXLEN)
        return false;
    strcpy(client->broker, broker);

    if (port == 0 || port > 65535)
        return false;
    client->port = port;

    if (strlen(client_id) >= MQTT_CLIENTID_MAXLEN)
        return false;
    strcpy(client->client_id, client_id);

    if (strlen(topic) >= MQTT_TOPIC_MAXLEN)
        return false;
    strcpy(client->topic, topic);

    client->connected = false;
    client->new_msg   = false;
    memset(client->json_buf, 0, MQTT_JSON_BUF_LEN);

    return true;
}

static const uint8_t mqtt_connect_pkt[] = {
    0x10, 0x12,
    0x00, 0x04, 'M', 'Q', 'T', 'T',
    0x04,
    0x02,
    0x00, 0x3C,
    0x00, 0x04, 't', 'e', 's', 't'
};

#define MQTT_CONNECT_PKT_SIZE sizeof(mqtt_connect_pkt)

/**
 * @brief 建立到MQTT Broker的TCP连接
 */
bool MQTT_ConnectToBroker(void)
{
    char cmd[64];
    
    ESP8266_ClearRx();
    
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    
    HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 1000);
    
    uint32_t start = HAL_GetTick();
    bool got_connect = false;
    bool got_error = false;
    
    while (HAL_GetTick() - start < 10000) {
        if (esp8266_rx_len > 0) {
            esp8266_rx_buf[esp8266_rx_len] = '\0';
            char *rx = (char *)esp8266_rx_buf;
            
            if (strstr(rx, "CONNECT") || strstr(rx, "OK")) {
                got_connect = true;
            }
            
            if (strstr(rx, "ERROR") || strstr(rx, "FAIL") || strstr(rx, "CLOSED")) {
                got_error = true;
            }
            
            if (got_connect && !got_error) {
                return true;
            }
            
            if (got_error) {
                return false;
            }
        }
        HAL_Delay(50);
    }
    
    return false;
}

/**
 * @brief 发送MQTT CONNECT包
 */
bool MQTT_SendConnectPacket(void)
{
    char cmd[32];
    
    ESP8266_ClearRx();
    
    sprintf(cmd, "AT+CIPSEND=%d\r\n", MQTT_CONNECT_PKT_SIZE);
    HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 1000);
    
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 3000) {
        if (esp8266_rx_len > 0) {
            esp8266_rx_buf[esp8266_rx_len] = '\0';
            if (strstr((char *)esp8266_rx_buf, ">")) {
                break;
            }
            if (strstr((char *)esp8266_rx_buf, "ERROR")) {
                return false;
            }
        }
        HAL_Delay(10);
    }
    
    HAL_UART_Transmit(&huart2, (uint8_t *)mqtt_connect_pkt, MQTT_CONNECT_PKT_SIZE, 1000);
    
    start = HAL_GetTick();
    while (HAL_GetTick() - start < 3000) {
        if (esp8266_rx_len > 0) {
            esp8266_rx_buf[esp8266_rx_len] = '\0';
            if (strstr((char *)esp8266_rx_buf, "SEND OK")) {
                return true;
            }
            if (strstr((char *)esp8266_rx_buf, "ERROR") || 
                strstr((char *)esp8266_rx_buf, "FAIL")) {
                return false;
            }
        }
        HAL_Delay(10);
    }
    
    return false;
}

/**
 * @brief 完整的MQTT连接流程
 */
bool MQTT_FullConnect(MQTT_Client *client)
{
    if (!client) return false;
    
    if (!MQTT_ConnectToBroker()) {
        return false;
    }
    
    HAL_Delay(1000);
    
    if (!MQTT_SendConnectPacket()) {
        return false;
    }
    
    HAL_Delay(200);
    
    if (!MQTT_Wait_CONNACK(5000)) {
        return false;
    }
    
    client->connected = true;
    return true;
}

/**
 * @brief 轻量级 MQTT 循环处理函数
 */
void MQTT_Yield(MQTT_Client *client, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (HAL_GetTick() - start < timeout_ms) {
        if (esp8266_rx_len > 0) {
            uint16_t len = esp8266_rx_len;
            if (len > 127) len = 127;
            memcpy(client->json_buf, esp8266_rx_buf, len);
            client->json_buf[len] = '\0';
            esp8266_rx_len = 0;
            client->new_msg = true;
        }
    }
}

/* ================= 订阅 ================= */
bool MQTT_Subscribe(MQTT_Client *client)
{
    if (!client || !client->connected)
        return false;

    uint8_t sub_pkt[128];
    uint16_t len = 0;
    uint16_t topic_len = strlen(client->topic);

    if (topic_len + 5 > sizeof(sub_pkt))
        return false;

    sub_pkt[len++] = 0x82;
    sub_pkt[len++] = topic_len + 3;

    sub_pkt[len++] = (MQTT_SUB_PACKET_ID >> 8) & 0xFF;
    sub_pkt[len++] = MQTT_SUB_PACKET_ID & 0xFF;

    sub_pkt[len++] = (topic_len >> 8) & 0xFF;
    sub_pkt[len++] = topic_len & 0xFF;
    memcpy(&sub_pkt[len], client->topic, topic_len);
    len += topic_len;

    sub_pkt[len++] = 0x00;

    if (!ESP8266_SendRaw(sub_pkt, len)) {
        return false;
    }

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
void MQTT_HandleIncomingData(MQTT_Client *client, const char *raw)
{
    if (!client || !raw) return;

    if (raw[0] == '{') {
        strncpy(client->json_buf, raw, MQTT_JSON_BUF_LEN - 1);
        client->json_buf[MQTT_JSON_BUF_LEN - 1] = '\0';
        client->new_msg = true;
        return;
    }

    const char *json = strchr(raw, '{');
    if (json) {
        strncpy(client->json_buf, json, MQTT_JSON_BUF_LEN - 1);
        client->json_buf[MQTT_JSON_BUF_LEN - 1] = '\0';
        client->new_msg = true;
    }
}

bool MQTT_Wait_CONNACK(uint32_t timeout_ms)
{
    uint32_t tick = HAL_GetTick();

    while (HAL_GetTick() - tick < timeout_ms) {
        if (esp8266_rx_len >= 4) {
            for (uint16_t i = 0; i + 4 <= esp8266_rx_len; i++) {
                if (esp8266_rx_buf[i] == 0x20 &&
                    esp8266_rx_buf[i+1] == 0x02 &&
                    esp8266_rx_buf[i+3] == 0x00) {
                    
                    uint16_t remaining = esp8266_rx_len - (i + 4);
                    if (remaining > 0) {
                        memmove(esp8266_rx_buf, &esp8266_rx_buf[i+4], remaining);
                    }
                    esp8266_rx_len = remaining;
                    
                    return true;
                }
            }
        }
        HAL_Delay(10);
    }

    return false;
}
