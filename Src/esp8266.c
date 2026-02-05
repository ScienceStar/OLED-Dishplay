#include "esp8266.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================= 全局变量 ================= */
uint8_t esp8266_rx_byte;

ESP8266_Line esp8266_lines[ESP8266_LINE_NUM];
volatile uint8_t esp8266_line_write_index = 0;
volatile uint8_t esp8266_line_read_index  = 0;

/* ================== WiFi 自动重连 ================== */
uint32_t wifi_reconnect_tick = 0;
#define WIFI_RECONNECT_INTERVAL 5000

/* ================== UART & ESP8266 ================== */
uint8_t UartRxData;
uint8_t UartRxbuf[1024], UartIntRxbuf[1024];
uint16_t UartRxIndex = 0, UartRxFlag = 0, UartRxLen = 0, UartRxOKFlag = 0, UartIntRxLen = 0;
volatile uint8_t WiFiStatus; // 0=断开,1=连接
int8_t WiFiRSSI = 0;

/* 原始接收缓冲（给 MQTT / 透明数据用） */
uint8_t  esp8266_rx_buf[ESP8266_RX_MAX];
volatile uint16_t esp8266_rx_len = 0;
volatile uint8_t esp8266_rx_ok = 0;

/* ================= 初始化 ================= */
bool ESP8266_Init(void)
{
    esp8266_line_write_index = 0;
    esp8266_line_read_index  = 0;
    esp8266_rx_len           = 0;

    for (int i = 0; i < ESP8266_LINE_NUM; i++) {
        esp8266_lines[i].ready = 0;
        esp8266_lines[i].len   = 0;
        esp8266_lines[i].line[0] = '\0';
    }

    /* UART 接收常开（只启动一次） */
    HAL_UART_Receive_IT(&ESP8266_UART, &esp8266_rx_byte, 1);
    return true;
}

/* ================= 清空接收（只动行缓冲） ================= */
void ESP8266_ClearRx(void)
{
    for (int i = 0; i < ESP8266_LINE_NUM; i++) {
        esp8266_lines[i].ready = 0;
        esp8266_lines[i].len   = 0;
    }
    esp8266_line_read_index = esp8266_line_write_index;
}

/* ================= 串口发送 + 等待 ================= */
bool ESP8266_SendAndWait(const char *cmd,
                         const char *ack,
                         uint32_t timeout)
{
    ESP8266_ClearRx();

    HAL_UART_Transmit(&ESP8266_UART,
                      (uint8_t *)cmd,
                      strlen(cmd),
                      1000);

    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout) {

        if (esp8266_lines[esp8266_line_read_index].ready) {

            ESP8266_Line *line =
                &esp8266_lines[esp8266_line_read_index];

            printf("[ESP] %s\n", line->line);

            if (strstr(line->line, ack)) {
                line->ready = 0;
                esp8266_line_read_index =
                    (esp8266_line_read_index + 1) % ESP8266_LINE_NUM;
                return true;
            }

            line->ready = 0;
            esp8266_line_read_index =
                (esp8266_line_read_index + 1) % ESP8266_LINE_NUM;
        } else {
            HAL_Delay(1);
        }
    }

    return false;
}

/* ================= AT / WiFi / TCP ================= */
bool ESP8266_AT_Test(void)
{
    return ESP8266_SendAndWait("AT\r\n", "OK", 1000);
}

bool ESP8266_SetMode(ESP8266_Mode mode)
{
    char cmd[32];
    sprintf(cmd, "AT+CWMODE=%d\r\n", mode);
    return ESP8266_SendAndWait(cmd, "OK", 1000);
}

bool ESP8266_JoinAP(const char *ssid, const char *pwd)
{
    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    return ESP8266_SendAndWait(cmd, "WIFI GOT IP", 8000);
}

bool ESP8266_GetRSSI(void)
{
    ESP8266_ClearRx();

    // 发送查询命令
    HAL_UART_Transmit(&ESP8266_UART,
                      (uint8_t *)"AT+CWJAP?\r\n",
                      strlen("AT+CWJAP?\r\n"),
                      1000);

    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < 2000) {

        if (esp8266_lines[esp8266_line_read_index].ready) {

            ESP8266_Line *line =
                &esp8266_lines[esp8266_line_read_index];

            printf("[ESP] %s\n", line->line);

            // 查找 +CWJAP 行
            if (strstr(line->line, "+CWJAP:")) {

                /*
                 * 示例：
                 * +CWJAP:"SSID","MAC",channel,-57
                 */
                char *last_comma = strrchr(line->line, ',');

                if (last_comma) {
                    WiFiRSSI = (int8_t)atoi(last_comma + 1);
                }

                line->ready = 0;
                esp8266_line_read_index =
                    (esp8266_line_read_index + 1) % ESP8266_LINE_NUM;

                return true;
            }

            line->ready = 0;
            esp8266_line_read_index =
                (esp8266_line_read_index + 1) % ESP8266_LINE_NUM;
        } else {
            HAL_Delay(1);
        }
    }

    return false;
}

bool ESP8266_TCP_Connect(const char *ip, uint16_t port)
{
    char cmd[64];
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ip, port);
    return ESP8266_SendAndWait(cmd, "CONNECT", 5000);
}

bool ESP8266_TCP_Send(const char *data)
{
    char cmd[32];
    sprintf(cmd, "AT+CIPSEND=%d\r\n", (int)strlen(data));

    if (!ESP8266_SendAndWait(cmd, ">", 2000))
        return false;

    HAL_UART_Transmit(&ESP8266_UART,
                      (uint8_t *)data,
                      strlen(data),
                      1000);

    return ESP8266_SendAndWait("", "SEND OK", 3000);
}

/* ================= 发送原始二进制数据 ================= */
bool ESP8266_SendRaw(uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return false;

    char cmd[32];

    // 1. 发送 CIPSEND 命令，告诉模块即将发送 len 字节数据
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);

    // 等待 '>' 提示符
    if (!ESP8266_SendAndWait(cmd, ">", 3000))
        return false;

    // 2. 发送原始数据
    HAL_UART_Transmit(&ESP8266_UART, data, len, 1000);

    // 3. 等待 SEND OK 确认
    if (!ESP8266_SendAndWait("", "SEND OK", 2000))
        return false;

    return true;
}

/* ================= 进入透传模式 ================= */
bool ESP8266_TCP_EnterTransparent(void)
{
    // 设置透传模式
    if (!ESP8266_SendAndWait("AT+CIPMODE=1\r\n", "OK", 1000))
        return false;

    // 开始 TCP 连接前，请确保连接成功
    HAL_Delay(50); // 小延时保证模块稳定

    return true;
}

/* ================= 退出透传模式 ================= */
void ESP8266_TCP_ExitTransparent(void)
{
    // 退出透传模式需要发送 "+++"
    // 注意：发送前至少 1 秒不要发数据，否则模块会误认为数据仍在透传
    HAL_Delay(1100); // ESP8266 规范要求至少 1 秒静默

    const char exit_seq[] = "+++";
    HAL_UART_Transmit(&ESP8266_UART, (uint8_t *)exit_seq, sizeof(exit_seq) - 1, 1000);

    // 发送完后等待模块返回 OK
    ESP8266_SendAndWait("", "OK", 2000);
}

/* ================= 字节 → 行（核心修复点） ================= */
void ESP8266_RxHandler(uint8_t ch)
{
    /* ===== 1. 原始流 ===== */

    if (esp8266_rx_len < ESP8266_RX_MAX - 1) {
        esp8266_rx_buf[esp8266_rx_len++] = ch;
        esp8266_rx_buf[esp8266_rx_len] = '\0';
    }

    /* ===== 2. 行模型 ===== */

    ESP8266_Line *line =
        &esp8266_lines[esp8266_line_write_index];

    /* ---- Prompt '>' ---- */
    if (ch == '>') {

        line->len = 0;
        line->line[line->len++] = '>';
        line->line[line->len]   = '\0';
        line->ready = 1;

        goto NEXT_LINE;
    }

    /* ---- 忽略 \r ---- */
    if (ch == '\r')
        return;

    /* ---- 行结束 ---- */
    if (ch == '\n') {

        if (line->len == 0)
            return;   // 忽略空行

        line->line[line->len] = '\0';
        line->ready = 1;

        goto NEXT_LINE;
    }

    /* ---- 普通字符 ---- */
    if (line->len < ESP8266_LINE_MAX - 1) {
        line->line[line->len++] = ch;
    }

    return;


/* ===== 切行 ===== */
NEXT_LINE:

    uint8_t next =
        (esp8266_line_write_index + 1) % ESP8266_LINE_NUM;

    if (next == esp8266_line_read_index) {
        esp8266_line_read_index =
            (esp8266_line_read_index + 1) % ESP8266_LINE_NUM;
    }

    esp8266_line_write_index = next;

    ESP8266_Line *nline =
        &esp8266_lines[esp8266_line_write_index];

    nline->len   = 0;
    nline->ready = 0;
    nline->line[0] = '\0';
}