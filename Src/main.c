#include "main.h"
#include "oled.h"
#include "esp8266.h"
#include "cabinet_view.h"
#include "string.h"
#include <stdio.h>
#include <math.h>  // 用于正弦渐变

#ifndef M_PI
#define M_PI 3.14159265f
#endif

/* ================== Morse LED Config ================== */
#define DOT_DURATION  300
#define DASH_DURATION (DOT_DURATION * 3)
#define SYMBOL_SPACE  DOT_DURATION
#define LETTER_SPACE  (DOT_DURATION * 3)
#define WORD_SPACE    (DOT_DURATION * 7)
#define BREATH_STEPS  50 // 渐变步数，越多越平滑

/* ================== UART / MQTT Buffers ================== */
extern char mqtt_rx_buf[256];    // ESP8266 MQTT payload
volatile uint8_t WiFiStatus = 0; // 0=未连接,1=已连接
int8_t WiFiRSSI             = -100;
extern volatile uint8_t TcpClosedFlag;
extern UART_HandleTypeDef huart2;

/* ================== Function Prototypes ================== */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void breathe_led_smooth(uint32_t duration);
void morse_dot(void);
void morse_dash(void);
void morse_send(const char *text);
const char *get_morse(char c);
void esp8266_led_update(void);

/* ================== Morse Table ================== */
typedef struct {
    char c;
    const char *morse;
} MorseMap;

MorseMap morse_table[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."}, {'F', "..-."}, 
    {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."}, 
    {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, 
    {'S', "..."}, {'T', "-"}, {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, 
    {'Y', "-.--"}, {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, 
    {'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, 
    {'8', "---.."}, {'9', "----."}, {' ', " "}
};

const char *get_morse(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseMap); i++)
        if (morse_table[i].c == c) return morse_table[i].morse;
    return "";
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
    uint8_t boot_msg[] = "Booting...";
    OLED_ShowString(0, 0, boot_msg);
    OLED_Refresh();  // 刷新显示

    ESP8266_Init();
    ESP8266_MQTT_Init();
    CabinetView_Init();

    uint32_t last_oled_tick  = HAL_GetTick();
    uint32_t last_morse_tick = HAL_GetTick();

    while (1) {
        uint32_t now = HAL_GetTick();

        /* ---------- MQTT数据处理 ---------- */
        if (ESP8266_MQTT_HasMsg()) {
            char *json = ESP8266_MQTT_GetPayload();
            if (json) CabinetView_UpdateFromJson(json);
        }

        /* ---------- OLED滚动显示 ---------- */
        if (now - last_oled_tick >= 300) {
            CabinetView_ScrollTask();
            last_oled_tick = now;
        }

        /* ---------- 摩尔斯呼吸灯 ---------- */
        if (now - last_morse_tick >= 5000) {
            morse_send("SOS");
            last_morse_tick = now;
        }

        /* ---------- ESP8266 LED状态 ---------- */
        esp8266_led_update();
    }
}

/* ================== Morse LED Functions ================== */
void breathe_led_smooth(uint32_t duration)
{
    // 软件PWM，正弦渐变
    for (int step = 0; step < BREATH_STEPS; step++) {
        float brightness = sinf(M_PI * step / (BREATH_STEPS - 1)); // 0~1
        if (brightness < 0.05f) brightness = 0.05f;                // 保底亮度
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
        HAL_Delay(duration * brightness);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        HAL_Delay(duration * (1.0f - brightness));
    }
}

void morse_dot(void)
{
    breathe_led_smooth(DOT_DURATION);
    HAL_Delay(SYMBOL_SPACE);
}

void morse_dash(void)
{
    breathe_led_smooth(DASH_DURATION);
    HAL_Delay(SYMBOL_SPACE);
}

void morse_send(const char *text)
{
    while (*text) {
        if (*text == ' ')
            HAL_Delay(WORD_SPACE);
        else {
            const char *code = get_morse(*text);
            while (*code) {
                if (*code == '.')
                    morse_dot();
                else if (*code == '-')
                    morse_dash();
                code++;
            }
            HAL_Delay(LETTER_SPACE - SYMBOL_SPACE);
        }
        text++;
    }
}

/* ================== ESP8266 LED Update ================== */
void esp8266_led_update(void)
{
    static uint32_t last_toggle_tick = 0;
    uint32_t now                     = HAL_GetTick();

    if (WiFiStatus == 0) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        last_toggle_tick = now;
        return;
    }

    if (WiFiRSSI >= -50)
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    else if (WiFiRSSI >= -70 && (now - last_toggle_tick) >= 50) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        last_toggle_tick = now;
    } else if (WiFiRSSI < -70 && (now - last_toggle_tick) >= 200) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        last_toggle_tick = now;
    }
}

/* ================== Peripheral Init ================== */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
}

/* ================== System Clock ================== */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
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
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
        HAL_Delay(200);
    }
}