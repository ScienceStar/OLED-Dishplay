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
    uint32_t duration;
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

#define WIFI_RECONNECT_INTERVAL 5000
void ESP_WiFi_ReconnectTask(void)
{
    uint32_t now = HAL_GetTick();
    if (!WiFiStatus && now - wifi_reconnect_tick >= WIFI_RECONNECT_INTERVAL) {
        wifi_reconnect_tick = now;
        ESP8266_JoinAP(WIFI_SSID, WIFI_PWD);
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

    uint32_t last_morse_tick  = HAL_GetTick();
    uint32_t last_led_tick    = HAL_GetTick();
    uint32_t last_scroll_tick = HAL_GetTick();

    CabinetView_Init();

    // MQTT 初始化
    MQTT_Init(&mqttClient, MQTT_BROKER_HOST, MQTT_BROKER_PORT, "STM32_Client1", "cabinet.bridge.to.device");
    MQTT_Connect(&mqttClient);
    MQTT_Subscribe(&mqttClient);

    /* 摩尔斯呼吸灯步数 */
    static uint8_t breath_step = 0;
    static int8_t breath_dir = 1;

    while (1) {
        uint32_t now = HAL_GetTick();

        /* ---------- WiFi重连 ---------- */
        ESP_WiFi_ReconnectTask();

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

        /* ---------- 摩尔斯呼吸灯 ---------- */
        if (now - last_morse_tick >= 5000 && !morse_state.busy) {
            morse_state.code     = get_morse('SOS');
            morse_state.index    = 0;
            morse_state.busy     = 1;
            morse_state.duration = DOT_DURATION;
            last_morse_tick      = now;
        }

        if (morse_state.busy) {
            float brightness = (float)breath_step / BREATH_STEPS;
            if (brightness < 0.05f) brightness = 0.05f;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
            HAL_Delay((uint32_t)(morse_state.duration * brightness));
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
            HAL_Delay((uint32_t)(morse_state.duration * (1.0f - brightness)));

            breath_step += breath_dir;
            if (breath_step >= BREATH_STEPS) { breath_dir = -1; breath_step = BREATH_STEPS-1; }
            else if (breath_step <= 0) { breath_dir = 1; breath_step = 0; }

            static uint32_t symbol_delay_tick = 0;
            static uint8_t symbol_done = 0;
            if (!symbol_done && breath_step == 0) {
                symbol_done = 1;
                symbol_delay_tick = HAL_GetTick();
            }
            if (symbol_done && HAL_GetTick() - symbol_delay_tick >= SYMBOL_SPACE) {
                morse_state.index++;
                symbol_done = 0;
                if (morse_state.code[morse_state.index] == '\0') morse_state.busy = 0;
            }
        }

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

        HAL_Delay(30);
    }
}

/* ================== UART回调 ================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {

        /* ===== 1. 旧系统缓存 ===== */
        UartIntRxbuf[UartRxIndex++] = UartRxData;
        if (UartRxIndex >= 1024) UartRxIndex = 0;
        UartIntRxLen = UartRxIndex;
        UartRxFlag   = 0x55;
        UartRxOKFlag = 0x55;

        /* ===== 2. 喂给 ESP8266 行模型 ===== */
        ESP8266_RxHandler(UartRxData);   // ★★★★★关键缺失

        /* ===== 3. 重启接收 ===== */
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