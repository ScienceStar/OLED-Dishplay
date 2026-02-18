#include "main.h"
#include "esp8266.h"
#include "gpio.h"
#include "oled.h"
#include "tcp.h"
#include "usart.h"
#include "cabinet_view.h"
#include "mqtt.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================== Morse Config ================== */
#define DOT_DURATION   120
#define DASH_DURATION  (DOT_DURATION * 3)
#define SYMBOL_SPACE   DOT_DURATION
#define WORD_SPACE     (DOT_DURATION * 7)

/* ================== MQTT ================== */
#define MQTT_JSON_BUF_LEN 128
char mqtt_json_buf[MQTT_JSON_BUF_LEN] = {0};

/* ================== Morse状态机 ================== */
typedef struct {
    const char *code;
    uint8_t index;
    uint8_t led_on;
    uint32_t tick;
    uint32_t duration;
} MorseState;

/* SOS */
static MorseState morse = {"...---...",0,0,0,DOT_DURATION};

/* ================== 非阻塞摩尔斯（时间补偿修复版） ================== */
void Morse_Task(void)
{
    uint32_t now = HAL_GetTick();

    /* ---- 时间补偿防止被系统阻塞拉慢 ---- */
    if (now - morse.tick < morse.duration) return;
    morse.tick += morse.duration;

    /* ---- 一轮结束 ---- */
    if (morse.code[morse.index] == '\0') {
        morse.index = 0;
        morse.duration = WORD_SPACE;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        return;
    }

    if (!morse.led_on) {
        /* 点亮 */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
        morse.led_on = 1;

        if (morse.code[morse.index] == '.')
            morse.duration = DOT_DURATION;
        else
            morse.duration = DASH_DURATION;
    }
    else {
        /* 熄灭 */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        morse.led_on = 0;
        morse.index++;
        morse.duration = SYMBOL_SPACE;
    }
}

/* ================== ESP8266状态解析（完整修复） ================== */
void ESP8266_StatusParse(uint8_t *buf)
{
    /* ---------- WiFi ---------- */
    if (strstr((char *)buf, "WIFI CONNECTED") ||
        strstr((char *)buf, "WIFI GOT IP") ||
        strstr((char *)buf, "GOT IP"))
        WiFiStatus = 1;

    if (strstr((char *)buf, "WIFI DISCONNECT") ||
        strstr((char *)buf, "WIFI DISCONNECTED"))
        WiFiStatus = 0;

    /* ---------- RSSI 正确解析 ---------- */
    /*
       +CWJAP:"SSID","MAC",ch,rssi
       取最后一个逗号后数值
    */
    char *p = strstr((char *)buf, "+CWJAP:");
    if (p) {
        char *last = strrchr(p, ',');
        if (last) {
            WiFiRSSI = atoi(last + 1);
        }
    }
    /* +CWJAP: 出现也表示已连上 AP（有时不会同时打印 GOT IP） */
    if (p) {
        WiFiStatus = 1;
    }

    /* ---------- TCP状态 ---------- */
    if (strstr((char *)buf, "CLOSED") ||
        strstr((char *)buf, "CONNECT FAIL") ||
        strstr((char *)buf, "ERROR"))
    {
        TcpClosedFlag = 1;
        mqttClient.connected = 0;
    }

    /* 注意避免把 "CONNECT FAIL" 误识别为成功连接（包含子串 "CONNECT"） */
    if ((strstr((char *)buf, "ALREADY CONNECTED") ||
         strstr((char *)buf, "CONNECT") ||
         strstr((char *)buf, "CONNECTED"))
        && !strstr((char *)buf, "CONNECT FAIL")
        && !strstr((char *)buf, "ERROR"))
    {
        TcpClosedFlag = 0;
    }
}

/* ================== Main ================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    OLED_Init();
    OLED_Clear();
    ESP8266_Init();

    HAL_UART_Receive_IT(&huart2, &UartRxData, 1);

    CabinetView_Init();

    MQTT_Init(&mqttClient,
              MQTT_BROKER_HOST,
              MQTT_BROKER_PORT,
              "STM32_Client1",
              "cabinet.bridge.to.device");

    uint32_t last_led_tick    = 0;
    uint32_t last_scroll_tick = 0;
    uint32_t mqtt_tick        = 0;

    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* ===== 摩尔斯灯 ===== */
        Morse_Task();

        /* ===== WiFi重连 ===== */
        ESP_WiFi_ReconnectTask();

        /* ===== MQTT重连 ===== */
        if (WiFiStatus && !mqttClient.connected &&
            now - mqtt_tick > 3000)
        {
            mqtt_tick = now;
            if (MQTT_Connect(&mqttClient))
                MQTT_Subscribe(&mqttClient);
        }

        /* ===== 状态栏 ===== */
        char wifi_char = 'X';
        char tcp_char  = 'X';
        char mqtt_char = 'X';

        if (WiFiStatus) {
            if (WiFiRSSI >= -50) wifi_char = '*';
            else if (WiFiRSSI >= -70) wifi_char = '+';
            else wifi_char = '.';
        }

        if (!TcpClosedFlag && WiFiStatus) tcp_char = 'T';
        if (mqttClient.connected) mqtt_char = 'M';

        char status[24];
        sprintf(status, "W:%c T:%c M:%c",
                wifi_char, tcp_char, mqtt_char);

        OLED_ShowStringSmall(20, 0, (uint8_t *)status);

        /* ===== TCP ===== */
        TCP_Task();
        if (WiFiStatus && !TcpClosedFlag) {
            TCP_Send_Loop();
            TCP_Heartbeat();
        }

        /* ===== UART解析 ===== */
        if (UartRxOKFlag == 0x55) {
            UartRxOKFlag = 0;
            ESP8266_StatusParse(UartRxbuf);
        }

        /* ===== 板载LED ===== */
        if (now - last_led_tick > 200) {
            last_led_tick = now;

            if (!WiFiStatus)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            else if (WiFiRSSI >= -50)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            else
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }

        /* ===== MQTT ===== */
        MQTT_Yield(&mqttClient, 5);

        if (MQTT_MessageReceived(&mqttClient)) {
            CabinetView_UpdateFromJson(mqttClient.json_buf);
        }

        /* ===== OLED滚动 ===== */
        CabinetView_ScrollTaskSmall(0);

        if (now - last_scroll_tick > 300) {
            last_scroll_tick = now;
            CabinetView_RotateDisplay();
        }
    }
}

/* ================== UART回调 ================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {

        /* 转发给 ESP8266 接收处理，使 esp8266_rx_buf/lines 被填充 */
        ESP8266_RxHandler(UartRxData);

        /* 同时保留原有行缓冲逻辑（供旧代码使用） */
        UartRxbuf[UartRxIndex++] = UartRxData;
        if (UartRxIndex >= 1024) UartRxIndex = 0;

        if (UartRxData == '\n') {
            UartRxbuf[UartRxIndex] = '\0';
            UartRxOKFlag = 0x55;
            UartRxIndex = 0;
        }

        /* 继续接收下一个字节 */
        HAL_UART_Receive_IT(&huart2, &UartRxData, 1);
    }
}

/* ================== System Clock ================== */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;

    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK  |
        RCC_CLOCKTYPE_SYSCLK|
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

/* ================== Error ================== */
void Error_Handler(void)
{
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
        HAL_Delay(200);
    }
}