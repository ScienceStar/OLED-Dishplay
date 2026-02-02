#include "cabinet_view.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>
#include "mqtt.h"

/* ================== 显示缓冲 ================== */
static char line1[32];
static char line2[32];

/* ================== 内部滚动状态 ================== */
#define MAX_LINES       5
#define MAX_SCROLL_TEXT 128

static char scroll_lines[MAX_LINES][MAX_SCROLL_TEXT];
static uint8_t scroll_count = 0;
static uint8_t scroll_pos[MAX_LINES];
static uint8_t scroll_step = 0;

/* ================== 初始化显示 ================== */
void CabinetView_Init(void)
{
    OLED_Clear();
    strcpy(line1, "Waiting MQTT...");
    strcpy(line2, "No data");

    OLED_ShowStringSmall(0, 2, (u8 *)line1);
    OLED_ShowStringSmall(0, 4, (u8 *)line2);
}

/* ================== 从 MQTT JSON 更新显示 ================== */
void CabinetView_UpdateFromJson(char *json)
{
    static char last_json[MQTT_JSON_BUF_LEN] = {0}; // 缓存上一次显示的内容

    // 如果内容没变化，直接返回，避免重复刷新
    if (strcmp(json, last_json) == 0) return;

    char cell[8]   = {0};
    int open       = 0;
    int err        = 0;
    char line1[32] = {0};
    char line2[32] = {0};

    if (sscanf(json, "{\"cellId\":\"%7[^\"]\",\"opened\":%d,\"error\":%d}", cell, &open, &err) == 3) {
        snprintf(line1, sizeof(line1), "CellId %s  Opened:%d", cell, open);
        snprintf(line2, sizeof(line2), "Error:%d", err);

        CabinetView_ClearScrollLines();
        CabinetView_AddScrollLine(line1);
        CabinetView_AddScrollLine(line2);

        // 立即刷新 OLED
        OLED_ShowStringSmall(0, 0, (uint8_t *)line1);
        OLED_ShowStringSmall(0, 8, (uint8_t *)line2);

        // 更新缓存
        strncpy(last_json, json, MQTT_JSON_BUF_LEN - 1);
        last_json[MQTT_JSON_BUF_LEN - 1] = '\0';
    } else {
        printf("CabinetView_UpdateFromJson: JSON解析失败: %s\n", json);
    }
}

/* ================== 增加滚动行 ================== */
void CabinetView_AddScrollLine(const char *text)
{
    if (scroll_count >= MAX_LINES) return;
    strncpy(scroll_lines[scroll_count], text, MAX_SCROLL_TEXT - 1);
    scroll_lines[scroll_count][MAX_SCROLL_TEXT - 1] = '\0';
    scroll_pos[scroll_count]                        = 0;
    scroll_count++;
}

/* ================== 清空滚动行 ================== */
void CabinetView_ClearScrollLines(void)
{
    scroll_count = 0;
    for (int i = 0; i < MAX_LINES; i++) scroll_lines[i][0] = '\0';
    for (int i = 0; i < MAX_LINES; i++) scroll_pos[i] = 0;
}

/* ================== OLED 滚动显示任务 ================== */
void CabinetView_ScrollTask(void)
{
    OLED_Clear();
    if (scroll_step == 0) {
        OLED_ShowString(0, 0, (u8 *)line1);
        OLED_ShowString(0, 2, (u8 *)line2);
    } else {
        OLED_ShowString(0, 2, (u8 *)line1);
        OLED_ShowString(0, 4, (u8 *)line2);
    }

    scroll_step++;
    if (scroll_step > 1) scroll_step = 0;
}

/* ================== 小号滚动任务 ================== */
void CabinetView_ScrollTaskSmall(uint8_t start_row)
{
    if (scroll_count == 0) return;

    for (uint8_t i = 0; i < scroll_count; i++) {
        uint8_t row = start_row + i;
        if (row >= 8) break; // OLED 总共8行

        char buf[129];
        uint8_t len  = strlen(scroll_lines[i]);
        uint8_t left = len - scroll_pos[i];
        if (left > 128) left = 128;
        memcpy(buf, scroll_lines[i] + scroll_pos[i], left);
        buf[left] = '\0';

        OLED_ShowStringSmall(0, row, (u8 *)buf);

        scroll_pos[i]++;
        if (scroll_pos[i] >= len) scroll_pos[i] = 0;
    }
}