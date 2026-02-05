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
#define DOT_DURATION  200 // 点的持续时间（毫秒）
#define DASH_DURATION (DOT_DURATION * 3)
#define SYMBOL_SPACE  50 // 符号间隔（点与点之间的空白时间）
#define LETTER_SPACE  (DOT_DURATION * 3)
#define WORD_SPACE    (DOT_DURATION * 7)

/* ===================MQTT参数================================= */
#define MQTT_JSON_BUF_LEN 128
char mqtt_json_buf[MQTT_JSON_BUF_LEN] = {0};

/* ================== Morse状态机 ================== */
typedef enum {
    MORSE_IDLE,
    MORSE_ON,   // 灯亮
    MORSE_OFF   // 灯灭
} MorseStage;

typedef struct {
    const char *code;      // 摩尔斯码串
    uint8_t index;         // 当前符号索引
    MorseStage stage;      // 当前阶段
    uint32_t tick;         // 阶段开始时间
    uint32_t duration;     // 当前阶段持续时间
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
    {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"},
    {'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."},
    {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {' ', " "}
};

const char *get_morse(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; i < sizeof(morse_table)/sizeof(MorseMap); i++)
        if (morse_table[i].c == c) return morse_table[i].morse;
    return "";
}

/* ================== SOS摩尔斯序列 ================== */
const char *morse_sequence = "... --- ..."; // SOS

void Morse_Start(const char *sequence)
{
    morse_state.code = sequence;
    morse_state.index = 0;
    morse_state.stage = MORSE_IDLE;
    morse_state.tick  = HAL_GetTick();
    morse_state.duration = 0;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
}

void Morse_Task(void)
{
    if (!morse_state.code) return;
    uint32_t now = HAL_GetTick();

    char c = morse_state.code[morse_state.index];
    if (c == '\0') {
        morse_state.code = NULL;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        return;
    }

    switch (morse_state.stage) {
        case MORSE_IDLE:
        case MORSE_OFF:
            if (now - morse_state.tick >= morse_state.duration) {
                if (c == ' ') {
                    morse_state.duration = WORD_SPACE;
                    morse_state.tick = now;
                    morse_state.index++;
                    break;
                }
                morse_state.stage = MORSE_ON;
                morse_state.tick = now;
                morse_state.duration = (c == '.') ? DOT_DURATION : DASH_DURATION;
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
            }
            break;
        case MORSE_ON:
            if (now - morse_state.tick >= morse_state.duration) {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
                morse_state.stage = MORSE_OFF;
                morse_state.tick = now;
                morse_state.duration = SYMBOL_SPACE;
                morse_state.index++;
            }
            break;
    }
}

/* ================== WiFi重连 ================== */
#define WIFI_RECONNECT_INTERVAL 5000
void ESP_WiFi_ReconnectTask(void)
{
    static uint32_t wifi_reconnect_tick = 0;
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

    uint32_t last_led_tick    = HAL_GetTick();
    uint32_t last_scroll_tick = HAL_GetTick();
    uint32_t mqtt_connect_tick = 0;

    CabinetView_Init();
    MQTT_Init(&mqttClient, MQTT_BROKER_HOST, MQTT_BROKER_PORT,
              "STM32_Client1", "cabinet.bridge.to.device");

    static uint32_t last_morse_start = 0;
    while (1) {
        uint32_t now = HAL_GetTick();

        ESP_WiFi_ReconnectTask();

        if (WiFiStatus && !mqttClient.connected &&
            now - mqtt_connect_tick >= 3000) 
        {
            mqtt_connect_tick = now;
            if (MQTT_Connect(&mqttClient)) {
                MQTT_Subscribe(&mqttClient);
            }
        }

        // SOS闪烁
        if (!morse_state.code && now - last_morse_start >= 2000) {
            last_morse_start = now;
            Morse_Start(morse_sequence);
        }
        Morse_Task();

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

        TCP_Task();
        if (WiFiStatus && !TcpClosedFlag) {
            TCP_Send_Loop();
            TCP_Heartbeat();
        }

        if (UartRxOKFlag == 0x55) {
            UartRxOKFlag = 0;
            UartRxLen    = UartIntRxLen;
            memcpy(UartRxbuf, UartIntRxbuf, UartIntRxLen);
            UartIntRxLen = 0;

            if (strstr((char *)UartRxbuf, "WIFI CONNECTED")) WiFiStatus = 1;
            else if (strstr((char *)UartRxbuf, "WIFI DISCONNECTED")) WiFiStatus = 0;

            char *rssi_ptr = strstr((char *)UartRxbuf, "+CWJAP:");
            if (rssi_ptr) WiFiRSSI = atoi(rssi_ptr + 7);

            if (strstr((char *)UartRxbuf, "CLOSED\r\n")) {
                TcpClosedFlag        = 1;
                mqttClient.connected = 0;
            }
            UartRxIndex = 0;
        }

        if (now - last_led_tick >= 50) {
            last_led_tick = now;
            if (!WiFiStatus) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            else if (WiFiRSSI >= -50) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            else if (WiFiRSSI >= -70) HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            else HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }

        MQTT_Yield(&mqttClient, 50);
        if (MQTT_MessageReceived(&mqttClient)) {
            char json_local[MQTT_JSON_BUF_LEN];
            strncpy(json_local, mqttClient.json_buf, MQTT_JSON_BUF_LEN - 1);
            json_local[MQTT_JSON_BUF_LEN - 1] = '\0';
            CabinetView_UpdateFromJson(json_local);
        }

        CabinetView_ScrollTaskSmall(0);
        if (now - last_scroll_tick >= 300) {
            CabinetView_RotateDisplay();
            last_scroll_tick = now;
        }

        HAL_Delay(5);
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

        ESP8266_RxHandler(UartRxData);
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