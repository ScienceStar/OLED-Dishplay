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
#define DOT_DURATION  300
#define DASH_DURATION (DOT_DURATION * 3)
#define SYMBOL_SPACE  DOT_DURATION
#define LETTER_SPACE  (DOT_DURATION * 3)
#define WORD_SPACE    (DOT_DURATION * 7)
#define BREATH_STEPS  10

/* ===================MQTT参数================================= */
#define MQTT_JSON_BUF_LEN 128
char mqtt_json_buf[MQTT_JSON_BUF_LEN] = {0};

/* ================== Morse状态机 ================== */
typedef struct {
    const char *code;
    uint8_t index;
    uint8_t busy;
    uint8_t step;
    int8_t dir;
    uint32_t last_tick;
    uint8_t led_state;
    uint32_t duration;
    uint32_t symbol_tick;
} MorseState;
MorseState morse_state = {0};

/* ================== Morse Table ================== */
typedef struct {
    char c;
    const char *morse;
} MorseMap;
MorseMap morse_table[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
    {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
    {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."},
    {'9', "----."}, {' ', " "}
};

const char *get_morse(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseMap); i++)
        if (morse_table[i].c == c) return morse_table[i].morse;
    return "";
}

/* ================== Morse灯非阻塞更新 ================== */
void Morse_Update(void)
{
    if (!morse_state.busy) return;
    uint32_t now = HAL_GetTick();

    /* 呼吸灯步进 */
    if (now - morse_state.last_tick >= morse_state.duration / BREATH_STEPS) {
        morse_state.last_tick = now;
        morse_state.step += morse_state.dir;
        if (morse_state.step >= BREATH_STEPS) { morse_state.dir = -1; morse_state.step = BREATH_STEPS-1; }
        else if (morse_state.step <= 0) { morse_state.dir = 1; morse_state.step = 0; }

        /* 亮度映射到LED */
        float brightness = (float)morse_state.step / BREATH_STEPS;
        if (brightness < 0.05f) brightness = 0.05f;
        if (brightness > 0.95f) brightness = 0.95f;
        if (brightness >= 0.5f) morse_state.led_state = 1;
        else morse_state.led_state = 0;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, morse_state.led_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    /* 符号切换 */
    if (now - morse_state.symbol_tick >= SYMBOL_SPACE && morse_state.step == 0) {
        morse_state.symbol_tick = now;
        morse_state.index++;
        if (morse_state.code[morse_state.index] == '\0') morse_state.busy = 0;
        else morse_state.duration = (morse_state.code[morse_state.index] == '.') ? DOT_DURATION : DASH_DURATION;
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

    uint32_t last_led_tick    = HAL_GetTick();
    uint32_t last_scroll_tick = HAL_GetTick();
    uint32_t last_morse_start = HAL_GetTick();

    CabinetView_Init();

    // MQTT 初始化
    MQTT_Init(&mqttClient, MQTT_BROKER_HOST, MQTT_BROKER_PORT, "STM32_Client1", "cabinet.bridge.to.device");

    static uint32_t mqtt_connect_tick = 0;

    while (1) {
        uint32_t now = HAL_GetTick();

        /* ---------- WiFi重连 ---------- */
        ESP_WiFi_ReconnectTask();
        if (WiFiStatus && !mqttClient.connected && now - mqtt_connect_tick >= 3000) {
            mqtt_connect_tick = now;
            if (MQTT_Connect(&mqttClient)) {
                MQTT_Subscribe(&mqttClient);
            }
        }

        /* ---------- 状态栏 ---------- */
        char status_str[24];
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
        sprintf(status_str, "W:%c T:%c M:%c", wifi_char, tcp_char, mqtt_char);
        int x_pos = (128 - strlen(status_str) * 6) / 2;
        if (x_pos < 0) x_pos = 0;
        OLED_ShowStringSmall(x_pos, 0, (uint8_t *)status_str);

        /* ---------- 摩尔斯灯启动 ---------- */
        if (!morse_state.busy && now - last_morse_start >= 5000) {
            morse_state.code     = get_morse('SOS');
            morse_state.index    = 0;
            morse_state.busy     = 1;
            morse_state.step     = 0;
            morse_state.dir      = 1;
            morse_state.last_tick = now;
            morse_state.symbol_tick = now;
            morse_state.duration = DOT_DURATION;
            last_morse_start = now;
        }

        /* ---------- 摩尔斯灯更新（非阻塞） ---------- */
        Morse_Update();

        /* ---------- TCP ---------- */
        TCP_Task();
        if (WiFiStatus && !TcpClosedFlag) {
            TCP_Send_Loop();
            TCP_Heartbeat();
        }

        /* ---------- UART接收ESP8266 ---------- */
        if (UartRxOKFlag == 0x55) {
            UartRxOKFlag = 0;
            UartRxLen = UartIntRxLen;
            memcpy(UartRxbuf, UartIntRxbuf, UartIntRxLen);
            UartIntRxLen = 0;

            if (strstr((char *)UartRxbuf, "WIFI CONNECTED")) WiFiStatus = 1;
            else if (strstr((char *)UartRxbuf, "WIFI DISCONNECTED")) WiFiStatus = 0;

            char *rssi_ptr = strstr((char *)UartRxbuf, "+CWJAP:");
            if (rssi_ptr) WiFiRSSI = atoi(rssi_ptr + 7);

            if (strstr((char *)UartRxbuf, "CLOSED\r\n")) {
                TcpClosedFlag = 1;
                mqttClient.connected = 0;
            }
            UartRxIndex = 0;
        }

        /* ---------- ESP8266板载LED（PC13） ---------- */
        if (now - last_led_tick >= 50) {
            last_led_tick = now;
            if (!WiFiStatus) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            else if (WiFiRSSI >= -50) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            else if (WiFiRSSI >= -70) HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            else HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }

        /* ---------- MQTT处理 ---------- */
        MQTT_Yield(&mqttClient, 50);
        if (MQTT_MessageReceived(&mqttClient)) {
            char json_local[MQTT_JSON_BUF_LEN];
            strncpy(json_local, mqttClient.json_buf, MQTT_JSON_BUF_LEN-1);
            json_local[MQTT_JSON_BUF_LEN-1] = '\0';
            CabinetView_UpdateFromJson(json_local);
        }

        /* ---------- OLED滚动 ---------- */
        CabinetView_ScrollTaskSmall(0);
        if (now - last_scroll_tick >= 300) {
            CabinetView_RotateDisplay();
            last_scroll_tick = now;
        }

        HAL_Delay(30); // 主循环基本轮询间隔，不阻塞摩尔斯灯状态机
    }
}

/* ================== UART回调 ================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        UartIntRxbuf[UartRxIndex++] = UartRxData;
        if (UartRxIndex >= 1024) UartRxIndex = 0;
        UartIntRxLen = UartRxIndex;
        UartRxFlag   = 0x55;
        UartRxOKFlag = 0x55;

        if (esp8266_rx_len < ESP8266_RX_MAX) {
            esp8266_rx_buf[esp8266_rx_len++] = UartRxData;
            if (UartRxData == '\n' || UartRxData == '\r' || UartRxData == '>') {
                esp8266_rx_buf[esp8266_rx_len] = '\0';
                esp8266_rx_ok                  = 1;
                esp8266_rx_len                  = 0;
            }
        }
        HAL_UART_Receive_IT(&huart2, &UartRxData, 1);
    }
}

/* ================== System Clock ================== */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitStruct.OscillatorType     = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState           = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue     = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState           = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState       = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource      = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL         = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

/* ================== Error Handler ================== */
void Error_Handler(void)
{
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
        HAL_Delay(200);
    }
}