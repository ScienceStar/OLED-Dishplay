#ifndef __CABINET_VIEW_H
#define __CABINET_VIEW_H

#include "main.h"
#include <stdint.h>

/* OLED 显示初始化 */
extern void CabinetView_Init(void);

/* 从 MQTT JSON 更新格口状态 */
extern void CabinetView_UpdateFromJson(char *json);

extern void CabinetView_RotateDisplay(void);

/* OLED 滚动显示任务（周期调用） */
extern void CabinetView_ScrollTask(void);

/* 小号滚动任务（从 start_row 行开始） */
extern void CabinetView_ScrollTaskSmall(uint8_t start_row);

/* 增加滚动行（内部方法，可用于JSON解析后） */
extern void CabinetView_AddScrollLine(const char *text);

/* 清空滚动行 */
extern void CabinetView_ClearScrollLines(void);

#endif