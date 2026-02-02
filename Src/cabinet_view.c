#include "cabinet_view.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>

/* 显示缓冲 */
static char line1[32];
static char line2[32];

/* 滚动控制 */
static uint8_t scroll_step = 0;

/**
 * @brief OLED 显示初始化
 */
void CabinetView_Init(void)
{
    OLED_Clear();

    strcpy(line1, "Waiting MQTT...");
    strcpy(line2, "No data");

    /* 初始显示 */
    OLED_ShowString(0, 0, (u8 *)line1);
    OLED_ShowString(0, 2, (u8 *)line2);
}

/**
 * @brief 从 MQTT JSON 数据中解析并更新显示内容
 * JSON 示例：
 * {"cell":"01","open":1,"err":0}
 */
void CabinetView_UpdateFromJson(char *json)
{
    char cell[8] = {0};
    int open = 0;
    int err  = 0;

    /* 简单 JSON 解析（嵌入式友好） */
    if (sscanf(json,
               "{\"cell\":\"%7[^\"]\",\"open\":%d,\"err\":%d}",
               cell, &open, &err) == 3)
    {
        snprintf(line1, sizeof(line1),
                 "Cell %s  Open:%d",
                 cell, open);

        snprintf(line2, sizeof(line2),
                 "Error:%d",
                 err);
    }
}

/**
 * @brief OLED 滚动显示任务
 * 建议 300~500ms 调用一次
 */
void CabinetView_ScrollTask(void)
{
    OLED_Clear();

    /*
     * OLED 行说明（128x64）：
     * y=0  -> 第1行
     * y=2  -> 第2行
     * y=4  -> 第3行
     */

    if (scroll_step == 0)
    {
        OLED_ShowString(0, 0, (u8 *)line1);
        OLED_ShowString(0, 2, (u8 *)line2);
    }
    else
    {
        OLED_ShowString(0, 2, (u8 *)line1);
        OLED_ShowString(0, 4, (u8 *)line2);
    }

    scroll_step++;
    if (scroll_step > 1)
        scroll_step = 0;
}