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
    static char last_json[MQTT_JSON_BUF_LEN] = {0};

    if (strcmp(json, last_json) == 0) return;

    char cell[8] = {0};
    int open = 0;
    int err = 0;
    char line1_tmp[32] = {0};
    char line2_tmp[32] = {0};

    if (sscanf(json, "{\"cellId\":\"%7[^\"]\",\"opened\":%d,\"error\":%d}", cell, &open, &err) == 3) {
        snprintf(line1_tmp, sizeof(line1_tmp), "Cell %s Open:%d", cell, open);
        snprintf(line2_tmp, sizeof(line2_tmp), "Error:%d", err);

        // 检查格口是否已存在
        uint8_t found = 0;
        for (uint8_t i = 0; i < scroll_count; i += 2) {
            if (strncmp(scroll_lines[i], line1_tmp, MAX_SCROLL_TEXT) == 0) {
                strncpy(scroll_lines[i+1], line2_tmp, MAX_SCROLL_TEXT-1);
                scroll_lines[i+1][MAX_SCROLL_TEXT-1] = '\0';
                found = 1;
                break;
            }
        }

        // 不存在就添加
        if (!found) {
            if (scroll_count + 2 <= MAX_LINES) {
                CabinetView_AddScrollLine(line1_tmp);
                CabinetView_AddScrollLine(line2_tmp);
            } else {
                // 超过 MAX_LINES，覆盖最旧的格口
                strncpy(scroll_lines[0], line1_tmp, MAX_SCROLL_TEXT-1);
                scroll_lines[0][MAX_SCROLL_TEXT-1] = '\0';
                strncpy(scroll_lines[1], line2_tmp, MAX_SCROLL_TEXT-1);
                scroll_lines[1][MAX_SCROLL_TEXT-1] = '\0';
            }
        }

        strncpy(last_json, json, MQTT_JSON_BUF_LEN-1);
        last_json[MQTT_JSON_BUF_LEN-1] = '\0';
    }
}

// OLED 每次刷新显示一组格口信息
void CabinetView_RotateDisplay(void)
{
    static uint8_t current_index = 0;
    if (scroll_count == 0) return;

    OLED_Clear();

    // 每次显示两个行：line1 + line2
    char *line1 = scroll_lines[current_index];
    char *line2 = scroll_lines[current_index+1];

    OLED_ShowStringSmall(0, 2, (u8 *)line1);
    OLED_ShowStringSmall(0, 4, (u8 *)line2);

    // 指向下一组格口
    current_index += 2;
    if (current_index >= scroll_count) current_index = 0;
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