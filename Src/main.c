#include "main.h"
#include "gpio.h"
#include "oled.h"
#include <string.h>
#include <stdint.h>

/* ================== Morse Config ================== */
#define DOT_DURATION     300
#define DASH_DURATION    (DOT_DURATION*3)
#define SYMBOL_SPACE     DOT_DURATION
#define LETTER_SPACE     (DOT_DURATION*3)
#define WORD_SPACE       (DOT_DURATION*7)
#define BREATH_STEPS     5

/* ================== Function Prototypes ================== */
void SystemClock_Config(void);
void breathe_led_smooth(uint32_t duration);
void morse_dot(void);
void morse_dash(void);
void morse_send(const char *text);
const char* get_morse(char c);

/* ================== Morse Table ================== */
typedef struct {
    char c;
    const char *morse;
} MorseMap;

MorseMap morse_table[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
    {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."},
    {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."},
    {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."},
    {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"},
    {'Y', "-.--"}, {'Z', "--.."},
    {'0',"-----"},{'1',".----"},{'2',"..---"},{'3',"...--"},{'4',"....-"},
    {'5',"....."},{'6',"-...."},{'7',"--..."},{'8',"---.."},{'9',"----."},
    {' ', " "}
};

const char* get_morse(char c)
{
    if(c >= 'a' && c <= 'z') c -= 32;
    for(int i=0;i<sizeof(morse_table)/sizeof(MorseMap);i++)
        if(morse_table[i].c == c) return morse_table[i].morse;
    return "";
}

/* ================== Elon Musk Biography ================== */
const char* bio_lines[] = {
    "Elon Musk, born June 28, 1971, in Pretoria, South Africa,",
    "is an entrepreneur, inventor, and engineer known for",
    "founding SpaceX, Tesla Motors, Neuralink, and The Boring Company.",
    "He studied physics and economics and emigrated to the US to pursue",
    "business and technology opportunities. Musk has been instrumental",
    "in popularizing electric vehicles, private space exploration,",
    "and sustainable energy solutions. He is recognized for ambitious",
    "projects such as the Hyperloop, Mars colonization, and AI research.",
    "Elon Musk's vision combines innovation in technology, energy, and",
    "transportation to reshape the future of humanity on Earth and beyond.",
    "He has faced both criticism and praise for his leadership style,",
    "public statements, and relentless pursuit of goals, often pushing",
    "the limits of conventional industry norms. Despite challenges,",
    "his ventures have profoundly impacted the automotive, space, and",
    "energy sectors, inspiring a new generation of engineers and innovators.",
    "Through SpaceX, he revolutionized rocket reuse, drastically reducing",
    "launch costs and opening possibilities for Mars exploration.",
    "Tesla accelerated the global shift to electric vehicles, leading",
    "innovations in battery technology and autonomous driving.",
    "Musk continues to pursue ambitious projects, advocating for",
    "sustainable energy, space exploration, and the development",
    "of artificial intelligence safety. His life illustrates the",
    "intersection of visionary thinking, technical expertise, and",
    "entrepreneurial drive in shaping the future."
};
#define BIO_LINE_COUNT (sizeof(bio_lines)/sizeof(bio_lines[0]))

/* ================== Main ================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    OLED_Init();
    OLED_Clear();

    int current_index = 0;
    uint32_t last_tick = HAL_GetTick();
    uint32_t last_morse_tick = HAL_GetTick();

    while(1)
    {
        uint32_t now = HAL_GetTick();

        // 每秒显示一行个人传记
        if(now - last_tick >= 1000)
        {
            OLED_Clear();
            OLED_ShowString(0,0,(uint8_t*)bio_lines[current_index]);
            current_index++;
            if(current_index >= BIO_LINE_COUNT) current_index = 0;
            last_tick = now;
        }

        // 摩尔斯电码每5秒闪一次
        if(now - last_morse_tick >= 5000)
        {
            morse_send("SOS");
            last_morse_tick = now;
        }
    }
}

/* ================== Morse LED ================== */
void breathe_led_smooth(uint32_t duration)
{
    uint32_t step_delay = duration/(2*BREATH_STEPS);
    for(int i=0;i<BREATH_STEPS;i++)
    {
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_4,GPIO_PIN_SET);
        HAL_Delay(step_delay);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_4,GPIO_PIN_RESET);
        HAL_Delay(step_delay);
    }
    for(int i=BREATH_STEPS;i>0;i--)
    {
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_4,GPIO_PIN_SET);
        HAL_Delay(step_delay);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_4,GPIO_PIN_RESET);
        HAL_Delay(step_delay);
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
    while(*text)
    {
        if(*text == ' ') HAL_Delay(WORD_SPACE);
        else
        {
            const char *code = get_morse(*text);
            while(*code)
            {
                if(*code == '.') morse_dot();
                else if(*code == '-') morse_dash();
                code++;
            }
            HAL_Delay(LETTER_SPACE-SYMBOL_SPACE);
        }
        text++;
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
    if(HAL_RCC_OscConfig(&RCC_OscInitStruct)!=HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                                       RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct,FLASH_LATENCY_2)!=HAL_OK) Error_Handler();
}

/* ================== Error Handler ================== */
void Error_Handler(void)
{
    while(1)
    {
        HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_4);
        HAL_Delay(200);
    }
}