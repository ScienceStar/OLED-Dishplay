#include "main.h"
#include "gpio.h"
#include "oled.h"
#include "usart.h"
#include "esp8266.h"
#include "esp8266_wifi_adapter.h"
#include "tcp.h"
#include "cabinet_view.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* ================== Morse Config ================== */
#define DOT_DURATION  300
#define DASH_DURATION (DOT_DURATION * 3)
#define SYMBOL_SPACE  DOT_DURATION
#define LETTER_SPACE  (DOT_DURATION * 3)
#define WORD_SPACE    (DOT_DURATION * 7)
#define BREATH_STEPS  10 // 增加步数提高呼吸灯平滑度

/* =================== MQTT ================== */
#define MQTT_JSON_BUF_LEN 128
char mqtt_json_buf[MQTT_JSON_BUF_LEN] = {0};

/* ================== Morse状态机 ================== */
typedef struct {
    const char *code;
    uint8_t index;
    uint8_t busy;
    uint8_t ready;
    uint32_t duration;
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

/* ================== 主函数 ================== */
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

    /* 初始化CabinetView */
    CabinetView_Init();

    /* 状态机时间 */
    uint32_t last_tick          = HAL_GetTick();
    uint32_t last_morse_tick    = HAL_GetTick();
    uint32_t last_led_tick      = HAL_GetTick();
    uint32_t last_scroll_tick   = HAL_GetTick();
    uint32_t last_tcp_send_tick = HAL_GetTick();

    int bio_index = 0;

    // MQTT 初始化
    MQTT_Init(&mqttClient, MQTT_BROKER_HOST, MQTT_BROKER_PORT, "STM32_Client1", "cabinet.bridge.to.device");
    MQTT_Connect(&mqttClient);
    MQTT_Subscribe(&mqttClient);
    while (1) {
        uint32_t now = HAL_GetTick();

        /* ================== OLED显示传记 + 状态 ================== */
        if (now - last_tick >= 1000) {
            OLED_Clear();
            CabinetView_ScrollTaskSmall(0);
            CabinetView_RotateDisplay();

            /* WiFi/TCP/MQTT 状态显示 */
            char status[16];
            char wifi_char = WiFiStatus ? ((WiFiRSSI >= -50)   ? '*'
                                           : (WiFiRSSI >= -70) ? '+'
                                                               : '.')
                                        : 'X';
            char tcp_char  = (!TcpClosedFlag && WiFiStatus) ? 'T' : 'X';
            char mqtt_char = mqttClient.connected ? 'M' : 'X'; // 这里根据你的MQTT连接标志填
            sprintf(status, "W:%c T:%c M:%c", wifi_char, tcp_char, mqtt_char);
            int x_pos = (128 - strlen(status) * 6) / 2;
            if (x_pos < 0) x_pos = 0;
            OLED_ShowStringSmall(x_pos, 0, (uint8_t *)status);

            last_tick = now;
        }

        /* ================== 摩尔斯电码 ================== */
        if (now - last_morse_tick >= 5000 && !morse_state.busy) {
            morse_state.code     = get_morse('S');
            morse_state.index    = 0;
            morse_state.busy     = 1;
            morse_state.duration = DOT_DURATION;
            last_morse_tick      = now;
        }

        if (morse_state.busy) {
            static uint32_t step_tick = 0;
            static int8_t step_dir    = 1;
            static uint8_t step       = 0;

            if (now - step_tick >= 20) // 呼吸灯更新周期更快
            {
                step_tick        = now;
                float brightness = (float)step / BREATH_STEPS;
                if (brightness < 0.05f) brightness = 0.05f;

                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
                HAL_Delay((uint32_t)(morse_state.duration * brightness));
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
                HAL_Delay((uint32_t)(morse_state.duration * (1.0f - brightness)));

                step += step_dir;
                if (step >= BREATH_STEPS) {
                    step_dir = -1;
                    step     = BREATH_STEPS - 1;
                } else if (step <= 0) {
                    step_dir = 1;
                    step     = 0;
                }

                static uint32_t sym_delay_tick = 0;
                static uint8_t sym_delay_done  = 0;
                if (!sym_delay_done && step == 0) {
                    sym_delay_done = 1;
                    sym_delay_tick = HAL_GetTick();
                }
                if (sym_delay_done && HAL_GetTick() - sym_delay_tick >= SYMBOL_SPACE) {
                    morse_state.index++;
                    sym_delay_done = 0;
                    if (morse_state.code[morse_state.index] == '\0')
                        morse_state.busy = 0;
                }
            }
        }

        /* ================== TCP任务 ================== */
        TCP_Task();
        if (WiFiStatus && !TcpClosedFlag) {
            TCP_Send_Loop();
            TCP_Heartbeat();
        }

        /* ================== UART接收ESP8266 ================== */
        if (UartRxOKFlag == 0x55) {
            UartRxOKFlag = 0;
            memcpy(UartRxbuf, UartIntRxbuf, UartIntRxLen);
            UartIntRxLen = 0;
            if (strstr((char *)UartRxbuf, "WIFI CONNECTED"))
                WiFiStatus = 1;
            else if (strstr((char *)UartRxbuf, "WIFI DISCONNECTED"))
                WiFiStatus = 0;
            char *rssi_ptr = strstr((char *)UartRxbuf, "+CWJAP:");
            if (rssi_ptr) WiFiRSSI = atoi(rssi_ptr + 7);
            if (strstr((char *)UartRxbuf, "CLOSED\r\n")) {
                TcpClosedFlag = 1;
            }
            UartRxIndex = 0;
        }

        /* ================== ESP8266 LED状态 ================== */
        if (now - last_led_tick >= 50) {
            last_led_tick = now;
            if (WiFiStatus == 0)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            else if (WiFiRSSI >= -50)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            else if (WiFiRSSI >= -70)
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            else
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }

        /* ================== MQTT处理 ================== */
        if (esp8266_rx_len > 0) {
            esp8266_rx_len = 0;
            MQTT_HandleIncomingData(&mqttClient, (char *)esp8266_rx_buf);
        }

        if (MQTT_MessageReceived(&mqttClient)) {
            char json_local[MQTT_JSON_BUF_LEN];
            strncpy(json_local, mqttClient.json_buf, MQTT_JSON_BUF_LEN - 1);
            json_local[MQTT_JSON_BUF_LEN - 1] = '\0';
            CabinetView_UpdateFromJson(json_local);
        }
        /* ---------- OLED 滚动显示 ---------- */
        if (now - last_scroll_tick >= 300) {
            CabinetView_ScrollTaskSmall(2);
            last_scroll_tick = now;
        }

        HAL_Delay(30);
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
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

/* ================== Error Handler ================== */
void Error_Handler(void)
{
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
        HAL_Delay(200);
    }
}