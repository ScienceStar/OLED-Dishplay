#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "oled.h"
#include "esp8266.h"
#include "tcp.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* ================= Morse Config ================= */
#define DOT_DURATION     300
#define DASH_DURATION    (DOT_DURATION * 3)
#define SYMBOL_SPACE     DOT_DURATION
#define LETTER_SPACE    (DOT_DURATION * 3)
#define WORD_SPACE      (DOT_DURATION * 7)
#define BREATH_STEPS    5

/* ================= Morse Table ================= */
typedef struct {
    char c;
    const char *morse;
} MorseMap;

static MorseMap morse_table[] = {
    {'A', ".-"},{'B', "-..."},{'C', "-.-."},{'D', "-.."},
    {'E', "."},{'F', "..-."},{'G', "--."},{'H', "...."},
    {'I', ".."},{'J', ".---"},{'K', "-.-"},{'L', ".-.."},
    {'M', "--"},{'N', "-."},{'O', "---"},{'P', ".--."},
    {'Q', "--.-"},{'R', ".-."},{'S', "..."},{'T', "-"},
    {'U', "..-"},{'V', "...-"},{'W', ".--"},{'X', "-..-"},
    {'Y', "-.--"},{'Z', "--.."},
    {'0',"-----"},{'1',".----"},{'2',"..---"},{'3',"...--"},
    {'4',"....-"},{'5',"....."},{'6',"-...."},{'7',"--..."},
    {'8',"---.."},{'9',"----."},{' ', " "}
};

/* ================= Biography ================= */
static const char* bio_lines[] = {
    "Elon Musk, born June 28, 1971, in Pretoria, South Africa,",
    "is an entrepreneur, inventor, and engineer known for",
    "founding SpaceX, Tesla Motors, Neuralink, and The Boring Company.",
    "He studied physics and economics and emigrated to the US.",
    "Musk accelerated EVs, private space, and sustainable energy.",
    "His vision reshapes transportation, energy, and space."
};
#define BIO_LINE_COUNT (sizeof(bio_lines)/sizeof(bio_lines[0]))

/* ================= UART / ESP8266 ================= */
uint8_t  UartRxData;
uint8_t  UartRxBuf[1024];
uint16_t UartRxLen = 0;
volatile uint8_t UartRxDone = 0;

uint8_t  WiFiStatus = 0;
int8_t   WiFiRSSI   = -99;
extern uint8_t TcpClosedFlag;

/* ================= Function Prototypes ================= */
void SystemClock_Config(void);
static const char* get_morse(char c);
static void morse_send(const char *text);
static void breathe_led(uint32_t duration);
static void esp8266_led_update(void);

/* ================= UART RX CALLBACK ================= */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        if (UartRxLen < sizeof(UartRxBuf))
        {
            UartRxBuf[UartRxLen++] = UartRxData;
        }
        UartRxDone = 1;
        HAL_UART_Receive_IT(&huart2, &UartRxData, 1);
    }
}

/* ================= MAIN ================= */
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

    uint32_t oled_tick  = HAL_GetTick();
    uint32_t morse_tick = HAL_GetTick();
    uint32_t led_tick   = HAL_GetTick();
    uint8_t  line_idx   = 0;

    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* ===== ESP8266 TCP 状态机 ===== */
        TCP_Task();

        /* ===== UART 数据解析 ===== */
        if (UartRxDone)
        {
            UartRxDone = 0;

            if (strstr((char*)UartRxBuf, "WIFI CONNECTED")) WiFiStatus = 1;
            if (strstr((char*)UartRxBuf, "WIFI DISCONNECTED")) WiFiStatus = 0;

            char *rssi = strstr((char*)UartRxBuf, "+CWJAP:");
            if (rssi) WiFiRSSI = atoi(rssi + 7);

            TcpClosedFlag = strstr((char*)UartRxBuf, "CLOSED") ? 1 : 0;

            UartRxLen = 0;
            memset(UartRxBuf, 0, sizeof(UartRxBuf));
        }

        /* ===== OLED Biography ===== */
        if (now - oled_tick >= 1000)
        {
            OLED_Clear();
            OLED_ShowString(0, 0, (uint8_t*)bio_lines[line_idx]);
            line_idx = (line_idx + 1) % BIO_LINE_COUNT;
            oled_tick = now;
        }

        /* ===== Morse SOS ===== */
        if (now - morse_tick >= 5000)
        {
            morse_send("SOS");
            morse_tick = now;
        }

        /* ===== WiFi LED ===== */
        if (now - led_tick >= 100)
        {
            esp8266_led_update();
            led_tick = now;
        }
    }
}

/* ================= Morse ================= */
static const char* get_morse(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    for (uint8_t i = 0; i < sizeof(morse_table)/sizeof(MorseMap); i++)
        if (morse_table[i].c == c) return morse_table[i].morse;
    return "";
}

static void breathe_led(uint32_t duration)
{
    uint32_t d = duration / (2 * BREATH_STEPS);
    for (int i = 0; i < BREATH_STEPS; i++)
    {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
        HAL_Delay(d);
    }
}

static void morse_send(const char *text)
{
    while (*text)
    {
        const char *code = get_morse(*text++);
        while (*code)
        {
            breathe_led((*code++ == '.') ? DOT_DURATION : DASH_DURATION);
            HAL_Delay(SYMBOL_SPACE);
        }
        HAL_Delay(LETTER_SPACE);
    }
}

/* ================= WiFi LED ================= */
static void esp8266_led_update(void)
{
    if (!WiFiStatus)
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    else
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13,
            (WiFiRSSI > -60) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* ================= Clock & Error ================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_HCLK  |
                                       RCC_CLOCKTYPE_PCLK1 |
                                       RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

void Error_Handler(void)
{
    while (1)
    {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
        HAL_Delay(200);
    }
}