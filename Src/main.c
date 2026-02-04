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
#define BREATH_STEPS  20 // 呼吸步数

/* ===================MQTT参数================================= */
#define MQTT_JSON_BUF_LEN 128
char mqtt_json_buf[MQTT_JSON_BUF_LEN] = {0};

/* ================== Morse状态机 ================== */
typedef struct {
    const char *code;
    uint8_t index;
    uint8_t busy;
    uint32_t duration;
    uint32_t tick_start;
    uint8_t led_on;
} MorseState;
MorseState morse_state = {0};

/* ================== Morse Table ================== */
typedef struct {
    char c;
    const char *morse;
} MorseMap;
MorseMap morse_table[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"}, {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"}, {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {' ', " "}};

const char *get_morse(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseMap); i++)
        if (morse_table[i].c == c) return morse_table[i].morse;
    return "";
}

/* ================== WiFi 重连 ================== */
#define WIFI_RECONNECT_INTERVAL 5000
extern uint32_t wifi_reconnect_tick;
void ESP_WiFi_ReconnectTask(void)
{
    uint32_t now = HAL_GetTick();
    if (!WiFiStatus && now - wifi_reconnect_tick >= WIFI_RECONNECT_INTERVAL) {
        wifi_reconnect_tick = now;
        ESP8266_JoinAP(WIFI_SSID, WIFI_PWD);
    }
}

/* ================== TIM3 PWM for Morse ================== */
extern TIM_HandleTypeDef htim3;

void Morse_PWM_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin   = GPIO_PIN_4;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    TIM_OC_InitTypeDef sConfigOC = {0};
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 72 - 1; // 1 MHz计数
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 255; // 8位PWM
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim3);

    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}

/* ================== Morse呼吸灯任务 ================== */
void Morse_Breath_Task(void)
{
    static uint8_t breath_step = 0;
    static int8_t breath_dir   = 1;
    static uint32_t last_pwm_tick = 0;

    if (!WiFiStatus || !morse_state.busy) {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
        return;
    }

    uint32_t now = HAL_GetTick();
    char sym = morse_state.code[morse_state.index];
    if (sym == '\0') { // 字符结束
        morse_state.busy = 0;
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
        return;
    }

    // 开/关状态机
    if (!morse_state.led_on) {
        // 间隔结束，点亮LED
        if (now - morse_state.tick_start >= SYMBOL_SPACE) {
            morse_state.led_on = 1;
            morse_state.tick_start = now;
            if (sym == '.') morse_state.duration = DOT_DURATION;
            else if (sym == '-') morse_state.duration = DASH_DURATION;
            else morse_state.duration = WORD_SPACE;
        } else {
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
            return;
        }
    }

    // LED开期间，PWM呼吸
    if (now - last_pwm_tick >= 20) { // PWM更新周期 20ms
        last_pwm_tick = now;
        uint32_t duty = (uint32_t)((float)breath_step / BREATH_STEPS * 255);
        if (duty < 30) duty = 30; // 最小亮度
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, duty);

        breath_step += breath_dir;
        if (breath_step >= BREATH_STEPS) { breath_dir = -1; breath_step = BREATH_STEPS - 1; }
        else if (breath_step <= 0) { breath_dir = 1; breath_step = 0; }
    }

    // 符号持续时间到，关闭LED，准备下一个符号
    if (now - morse_state.tick_start >= morse_state.duration) {
        morse_state.index++;
        morse_state.led_on     = 0;
        morse_state.tick_start = now;
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

    Morse_PWM_Init(); // 初始化PWM

    uint32_t last_morse_tick  = HAL_GetTick();
    uint32_t last_led_tick    = HAL_GetTick();
    uint32_t last_scroll_tick = HAL_GetTick();

    CabinetView_Init();

    // MQTT 初始化
    MQTT_Init(&mqttClient, MQTT_BROKER_HOST, MQTT_BROKER_PORT, "STM32_Client1", "cabinet.bridge.to.device");
    MQTT_Connect(&mqttClient);
    MQTT_Subscribe(&mqttClient);

    while (1) {
        uint32_t now = HAL_GetTick();

        ESP_WiFi_ReconnectTask();

        // 状态栏显示
        char status_str[24];
        char wifi_char = 'X';
        char tcp_char  = 'X';
        char mqtt_char = 'X';
        if (WiFiStatus) {
            if (WiFiRSSI >= -50)
                wifi_char = '*';
            else if (WiFiRSSI >= -70)
                wifi_char = '+';
            else
                wifi_char = '.';
        }
        if (!TcpClosedFlag && WiFiStatus) tcp_char = 'T';
        if (mqttClient.connected) mqtt_char = 'M';
        sprintf(status_str, "W:%c T:%c M:%c", wifi_char, tcp_char, mqtt_char);
        int x_pos = (128 - strlen(status_str) * 6) / 2;
        if (x_pos < 0) x_pos = 0;
        OLED_ShowStringSmall(x_pos, 0, (uint8_t *)status_str);

        Morse_Breath_Task(); // 摩尔斯灯任务

        // TCP处理
        TCP_Task();
        if (WiFiStatus && !TcpClosedFlag) {
            TCP_Send_Loop();
            TCP_Heartbeat();
        }

        // UART接收ESP8266
        if (UartRxOKFlag == 0x55) {
            UartRxOKFlag = 0;
            UartRxLen    = UartIntRxLen;
            memcpy(UartRxbuf, UartIntRxbuf, UartIntRxLen);
            UartIntRxLen = 0;
            if (strstr((char *)UartRxbuf, "WIFI CONNECTED"))
                WiFiStatus = 1;
            else if (strstr((char *)UartRxbuf, "WIFI DISCONNECTED"))
                WiFiStatus = 0;
            char *rssi_ptr = strstr((char *)UartRxbuf, "+CWJAP:");
            if (rssi_ptr) WiFiRSSI = atoi(rssi_ptr + 7);
            if (strstr((char *)UartRxbuf, "CLOSED\r\n")) {
                TcpClosedFlag        = 1;
                mqttClient.connected = 0;
            }
            UartRxIndex = 0;
        }

        // 板载LED
        if (now - last_led_tick >= 50) {
            last_led_tick = now;
            if (!WiFiStatus)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            else if (WiFiRSSI >= -50)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            else
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
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

        HAL_Delay(30);
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
                esp8266_rx_len                 = 0;
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