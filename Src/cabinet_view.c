#include "cabinet_view.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>
#include "mqtt.h"

/* ================== 显示缓冲 ================== */
static char line1[32];
static char line2[32];

/* ================== 内部滚动状态 ================== */
#define MAX_LINES       10  // 增加到10行，可以显示更多格口
#define MAX_SCROLL_TEXT 128

static char scroll_lines[MAX_LINES][MAX_SCROLL_TEXT];
static uint8_t scroll_count = 0;
static uint8_t scroll_pos[MAX_LINES];
static uint8_t scroll_step = 0;

// 滚动速度控制
static uint32_t last_scroll_tick = 0;
#define SCROLL_INTERVAL 150  // 每150ms滚动一次，调整这个值改变滚动速度

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
    if (json == NULL || strlen(json) == 0) return;
    
    static char last_json[MQTT_JSON_BUF_LEN] = {0};

    // 如果消息相同，跳过
    if (strcmp(json, last_json) == 0) return;

    char cell[16] = {0};
    int open = 0;
    int err = 0;
    char line1_tmp[64] = {0};
    char line2_tmp[64] = {0};

    // 尝试多种JSON格式解析
    int parsed = 0;
    
    // 格式1: {"cellId":"01","opened":1,"error":0}
    parsed = sscanf(json, "{\"cellId\":\"%15[^\"]\",\"opened\":%d,\"error\":%d}", 
                    cell, &open, &err);
    
    // 格式2: {"cell":"01","open":1,"err":0}
    if (parsed != 3) {
        parsed = sscanf(json, "{\"cell\":\"%15[^\"]\",\"open\":%d,\"err\":%d}", 
                        cell, &open, &err);
    }
    
    // 格式3: {"cell_id":"01","is_open":1,"error_code":0}
    if (parsed != 3) {
        parsed = sscanf(json, "{\"cell_id\":\"%15[^\"]\",\"is_open\":%d,\"error_code\":%d}", 
                        cell, &open, &err);
    }

    if (parsed == 3) {
        // 格式化显示文本
        snprintf(line1_tmp, sizeof(line1_tmp), "Cell-%s Open:%d", cell, open);
        snprintf(line2_tmp, sizeof(line2_tmp), "Error:%d", err);

        // 检查格口是否已存在
        uint8_t found = 0;
        for (uint8_t i = 0; i < scroll_count; i += 2) {
            if (strncmp(scroll_lines[i], line1_tmp, MAX_SCROLL_TEXT) == 0) {
                // 更新现有格口的状态
                strncpy(scroll_lines[i+1], line2_tmp, MAX_SCROLL_TEXT-1);
                scroll_lines[i+1][MAX_SCROLL_TEXT-1] = '\0';
                found = 1;
                break;
            }
        }

        // 不存在就添加新格口
        if (!found) {
            if (scroll_count + 2 <= MAX_LINES) {
                CabinetView_AddScrollLine(line1_tmp);
                CabinetView_AddScrollLine(line2_tmp);
            } else {
                // 超过最大行数，移除最旧的格口（前两个），然后添加新的
                for (uint8_t i = 2; i < scroll_count; i++) {
                    strncpy(scroll_lines[i-2], scroll_lines[i], MAX_SCROLL_TEXT-1);
                    scroll_lines[i-2][MAX_SCROLL_TEXT-1] = '\0';
                }
                scroll_count -= 2;
                
                // 添加新的格口
                CabinetView_AddScrollLine(line1_tmp);
                CabinetView_AddScrollLine(line2_tmp);
            }
        }

        // 保存最后一条消息
        strncpy(last_json, json, MQTT_JSON_BUF_LEN-1);
        last_json[MQTT_JSON_BUF_LEN-1] = '\0';
    }
}

// OLED 每次刷新显示一组格口信息（轮播模式）- 不清除顶栏
void CabinetView_RotateDisplay(void)
{
    static uint8_t current_index = 0;
    if (scroll_count == 0) return;

    // 只清除第2-7行，保留第0-1行（状态栏）
    for (u8 page = 2; page < 8; page++) {
        for (u8 col = 0; col < 128; col++) {
            OLED_GRAM[col][page] = 0;
        }
    }

    // 每次显示两个行：line1 + line2
    char *l1 = scroll_lines[current_index];
    char *l2 = (current_index + 1 < scroll_count) ? scroll_lines[current_index+1] : "";

    OLED_ShowStringSmall(0, 2, (u8 *)l1);
    OLED_ShowStringSmall(0, 4, (u8 *)l2);

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
    for (int i = 0; i < MAX_LINES; i++) {
        scroll_lines[i][0] = '\0';
        scroll_pos[i] = 0;
    }
    
    // 清除第2-7行的显示
    for (u8 page = 2; page < 8; page++) {
        for (u8 col = 0; col < 128; col++) {
            OLED_GRAM[col][page] = 0;
        }
    }
    OLED_Refresh();
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

/* ================== 小号滚动任务（水平滚动） ================== */
void CabinetView_ScrollTaskSmall(uint8_t start_row)
{
    if (scroll_count == 0) return;
    
    uint32_t now = HAL_GetTick();
    
    // 控制滚动速度
    if (now - last_scroll_tick < SCROLL_INTERVAL) {
        return;  // 还没到滚动时间
    }
    last_scroll_tick = now;

    // 如果有多个格口，使用轮播模式而不是水平滚动
    if (scroll_count >= 2) {
        // 轮播模式已经在CabinetView_RotateDisplay中处理
        return;
    }
    
    // 只有一行时，使用水平滚动
    for (uint8_t i = 0; i < scroll_count; i++) {
        uint8_t row = start_row + i;
        if (row >= 8) break; // OLED 总共8行

        char buf[129];
        uint8_t len  = strlen(scroll_lines[i]);
        
        if (len <= 21) {
            // 文本较短，不需要滚动，直接显示
            OLED_ShowStringSmall(0, row, (u8 *)scroll_lines[i]);
        } else {
            // 文本较长，水平滚动
            uint8_t left = len - scroll_pos[i];
            if (left > 21) left = 21;  // OLED一行约显示21个字符（6像素宽字体）
            
            memcpy(buf, scroll_lines[i] + scroll_pos[i], left);
            buf[left] = '\0';

            OLED_ShowStringSmall(0, row, (u8 *)buf);

            scroll_pos[i]++;
            if (scroll_pos[i] >= len) scroll_pos[i] = 0;
        }
    }
}

/* ================== 测试：模拟接收MQTT消息 ================== */
void CabinetView_TestSimulation(void)
{
    // 这个函数可以用于测试，模拟从服务器接收的数据
    static uint8_t test_index = 0;
    static uint32_t last_test_tick = 0;
    uint32_t now = HAL_GetTick();
    
    // 每3秒模拟一条消息
    if (now - last_test_tick < 3000) {
        return;
    }
    last_test_tick = now;
    
    char test_messages[][64] = {
        "{\"cellId\":\"01\",\"opened\":1,\"error\":0}",
        "{\"cellId\":\"02\",\"opened\":0,\"error\":0}",
        "{\"cellId\":\"03\",\"opened\":1,\"error\":1}",
        "{\"cellId\":\"04\",\"opened\":0,\"error\":0}",
        "{\"cellId\":\"05\",\"opened\":1,\"error\":0}",
    };
    
    CabinetView_UpdateFromJson(test_messages[test_index]);
    test_index = (test_index + 1) % 5;
}