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

/* ================== Morse Config (Optimized for Speed) ================== */
#define DOT_DURATION   100  // ?????????100ms (?120ms??????????????????)
#define DASH_DURATION  (DOT_DURATION * 3)
#define SYMBOL_SPACE   DOT_DURATION
#define WORD_SPACE     (DOT_DURATION * 7)

/* ================== MQTT ================== */
#define MQTT_JSON_BUF_LEN 128
char mqtt_json_buf[MQTT_JSON_BUF_LEN] = {0};

/* ================== Global Variables for Link Health ================== */
static uint32_t last_esp8266_activity_tick = 0; 
#define LINK_HEALTH_TIMEOUT_MS 30000  // Increased from 15s to 30s to prevent false timeouts

// ????????????????????
static char last_debug_status[64] = {0};

// ??????
static uint8_t connection_stage = 0; // 0=Init, 1=WiFi, 2=TCP, 3=MQTT, 4=Connected
static uint32_t last_wifi_connect_attempt = 0;
#define WIFI_CONNECT_COOLDOWN 10000  // 10????????????

// ??????MQTT???????????
#define ENABLE_TEST_MODE 1  // ???1?????0??

/* ================== Morse State ================== */
typedef struct {
    const char *code;
    uint8_t index;
    uint8_t led_on;
    uint32_t next_tick; // ??????????????????????????????????????
    uint32_t duration;
} MorseState;

/* SOS */
static MorseState morse = {"...---...", 0, 0, 0, DOT_DURATION};

/* ================== Morse Task (Precision Optimized) ================== */
void Morse_Task(void)
{
    uint32_t now = HAL_GetTick();

    // ??????????????????????????????????? CPU
    if (now < morse.next_tick) return;

    // ????????????????????????????? + ???????
    // ?????????? WiFi/MQTT ????????????????????????????
    morse.next_tick = now + morse.duration;

    /* ---- ?????????? ---- */
    if (morse.code[morse.index] == '\0') {
        morse.index = 0;
        morse.duration = WORD_SPACE;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        return;
    }

    if (!morse.led_on) {
        /* LED ON (??) */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
        morse.led_on = 1;

        if (morse.code[morse.index] == '.')
            morse.duration = DOT_DURATION;
        else
            morse.duration = DASH_DURATION;
    }
    else {
        /* LED OFF (??/????) */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        morse.led_on = 0;
        morse.index++;
        morse.duration = SYMBOL_SPACE;
    }
}

/* ================== ESP8266 Status Parse (Enhanced) ================== */
void ESP8266_StatusParse(uint8_t *buf)
{
    if (buf == NULL || strlen((char *)buf) == 0) return;

    // ?????? - ??ESP8266??????
    last_esp8266_activity_tick = HAL_GetTick();
    
    // ????????????
    strncpy(last_debug_status, (char*)buf, sizeof(last_debug_status)-1);
    last_debug_status[sizeof(last_debug_status)-1] = '\0';

    /* ---------- WiFi Status ---------- */
    if (strstr((char *)buf, "WIFI GOT IP") || strstr((char *)buf, "GOT IP")) {
        WiFiStatus = 1;
        connection_stage = 1;
        extern uint32_t wifi_reconnect_tick; 
        wifi_reconnect_tick = HAL_GetTick(); 
        last_wifi_connect_attempt = HAL_GetTick();
    }
    else if (strstr((char *)buf, "WIFI CONNECTED")) {
        WiFiStatus = 1; 
        connection_stage = 1;
    }

    // ??????
    if (strstr((char *)buf, "WIFI DISCONNECT") ||
        strstr((char *)buf, "WIFI DISCONNECTED")) 
    {
        WiFiStatus = 0;
        mqttClient.connected = 0;
        TcpClosedFlag = 1;  // Keep for internal use
        connection_stage = 0;
        extern uint32_t wifi_reconnect_tick;
        wifi_reconnect_tick = 0; 
    }

    /* ---------- RSSI Parsing ---------- */
    char *p = strstr((char *)buf, "+CWJAP:");
    if (p) {
        char *last = strrchr(p, ',');
        if (last) {
            WiFiRSSI = atoi(last + 1);
        }
        WiFiStatus = 1;
        connection_stage = 1;
    }

    /* ---------- MQTT/TCP Status ---------- */
    // For MQTT, we care about connection state
    if (strstr((char *)buf, "CLOSED") ||
        strstr((char *)buf, "CONNECT FAIL") ||
        strstr((char *)buf, "ERROR"))
    {
        mqttClient.connected = 0;
        TcpClosedFlag = 1;
        if (connection_stage >= 2) connection_stage = 1;
    }

    if ((strstr((char *)buf, "ALREADY CONNECTED") ||
         strstr((char *)buf, ",CONNECT") ||
         strstr((char *)buf, "LINKED"))
        && !strstr((char *)buf, "FAIL")
        && !strstr((char *)buf, "ERROR"))
    {
        TcpClosedFlag = 0;
        connection_stage = 2;
    }
    
    // MQTT data received
    if (strstr((char *)buf, "+IPD")) {
        TcpClosedFlag = 0;
        if (connection_stage < 2) connection_stage = 2;
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
    
    // ??????
    OLED_ShowStringSmall(0, 0, (uint8_t*)"System Boot...");
    OLED_ShowStringSmall(0, 2, (uint8_t*)"Initializing...");

    // 1. ??? ESP8266
    ESP8266_Init();

    // 2. ?? UART ??
    HAL_UART_Receive_IT(&huart2, &UartRxData, 1);

    CabinetView_Init();

    MQTT_Init(&mqttClient,
              MQTT_BROKER_HOST,
              MQTT_BROKER_PORT,
              "STM32_Client1",
              "cabinet.bridge.to.device");

    uint32_t last_led_tick    = 0;
    uint32_t last_scroll_tick = 0;  // ??????????
    uint32_t mqtt_tick        = 0;
    uint32_t wifi_reconnect_tick = 0;
    uint32_t last_status_update_tick = 0;  // ?????????
    
    last_esp8266_activity_tick = HAL_GetTick();
    last_wifi_connect_attempt = HAL_GetTick();
    
    // ?????????????
    morse.next_tick = HAL_GetTick();

    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* ===== Morse Task (????????????) ===== */
        Morse_Task();

        /* ===== WiFi Management - Simplified ===== */
        // Only attempt reconnection if WiFi is down AND cooldown period passed
        if (!WiFiStatus) {
            if (now - wifi_reconnect_tick > 5000 && 
                now - last_wifi_connect_attempt > WIFI_CONNECT_COOLDOWN) {
                wifi_reconnect_tick = now;
                last_wifi_connect_attempt = now;
                // Don't update OLED here to avoid flicker
                ESP_WiFi_ReconnectTask(); 
            }
        } else {
            // WiFi is connected, just do periodic health check
            ESP_WiFi_ReconnectTask();
        }

        /* ===== Link Health Check - More Lenient ===== */
        // Only timeout if we've been connected but received nothing for 30+ seconds
        if (WiFiStatus && (now - last_esp8266_activity_tick > LINK_HEALTH_TIMEOUT_MS)) {
            WiFiStatus = 0;
            TcpClosedFlag = 1;
            mqttClient.connected = 0;
            connection_stage = 0;
            wifi_reconnect_tick = 0; 
            strcpy(last_debug_status, "Link Timeout");
        }

        /* ===== Simplified Connection Logic - MQTT Only ===== */
        
        // If WiFi is up but MQTT not connected, try to connect MQTT
        if (WiFiStatus && !mqttClient.connected && (now - mqtt_tick > 10000)) {
            mqtt_tick = now;
            
            // Try to connect
            if (MQTT_FullConnect(&mqttClient)) {
                mqttClient.connected = true;
                MQTT_Subscribe(&mqttClient);
                connection_stage = 3;
            } else {
                mqttClient.connected = false;
                TcpClosedFlag = 1;
            }
        }
        
        // Send heartbeat if MQTT is connected
        if (WiFiStatus && mqttClient.connected) {
            TCP_Heartbeat();
        }

        /* ===== Status Display - Simple and stable ===== */
        if (now - last_status_update_tick > 2000) {
            last_status_update_tick = now;
            
            char wifi_char = 'X';
            char mqtt_char = 'X';

            if (WiFiStatus) {
                if (WiFiRSSI >= -50) wifi_char = '*';
                else if (WiFiRSSI >= -70) wifi_char = '+';
                else wifi_char = '.';
            }
            
            if (mqttClient.connected) {
                mqtt_char = 'M';
            }

            char status[24];
            sprintf(status, "W:%c M:%c      ", wifi_char, mqtt_char);
            OLED_ShowStringSmall(20, 0, (uint8_t *)status);
            
            char debug_line[24];
            if (!WiFiStatus) {
                 sprintf(debug_line, "WiFi:Not Conn ");
                 OLED_ShowStringSmall(0, 6, (uint8_t *)debug_line);
            } else if (!mqttClient.connected) {
#if ENABLE_TEST_MODE
                 sprintf(debug_line, "TEST MODE     ");
                 CabinetView_TestSimulation();
#else
                 sprintf(debug_line, "%.19s", "Connecting...   ");
#endif
                 OLED_ShowStringSmall(0, 6, (uint8_t *)debug_line);
            } else {
                 sprintf(debug_line, "MQTT Connected");
                 OLED_ShowStringSmall(0, 6, (uint8_t *)debug_line);
            }
        }

        /* ===== UART Data Processing ===== */
        if (UartRxOKFlag == 0x55) {
            __disable_irq();
            uint8_t local_buf[1024];
            strncpy((char*)local_buf, (char*)UartRxbuf, sizeof(local_buf)-1);
            local_buf[sizeof(local_buf)-1] = '\0';
            
            UartRxOKFlag = 0;
            UartRxIndex = 0; 
            __enable_irq();
            
            ESP8266_StatusParse(local_buf);
        }

        /* ===== LED Indicator ===== */
        if (now - last_led_tick > 200) {
            last_led_tick = now;
            if (!WiFiStatus)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   
            else if (WiFiRSSI >= -50)
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); 
            else
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);                
        }

        /* ===== MQTT Yield ===== */
        MQTT_Yield(&mqttClient, 5);

        if (MQTT_MessageReceived(&mqttClient)) {
            CabinetView_UpdateFromJson(mqttClient.json_buf);
        }

        /* ===== OLED Scroll & Rotate ===== */
        CabinetView_ScrollTaskSmall(0);
        if (now - last_scroll_tick > 3000) {
            last_scroll_tick = now;
            CabinetView_RotateDisplay();
        }
    }
}

/* ================== UART Callback ================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        ESP8266_RxHandler(UartRxData);

        if (UartRxIndex < 1023) {
            UartRxbuf[UartRxIndex++] = UartRxData;
        }

        if (UartRxData == '\n') {
            UartRxbuf[UartRxIndex] = '\0';
            UartRxOKFlag = 0x55;
            UartRxIndex = 0; 
        }

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

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

void Error_Handler(void)
{
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
        HAL_Delay(200);
    }
}