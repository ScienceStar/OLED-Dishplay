#ifndef __CABINET_VIEW_H
#define __CABINET_VIEW_H

#include "main.h"

/* 初始化显示内容 */
void CabinetView_Init(void);

/* 从 MQTT JSON 更新格口状态 */
void CabinetView_UpdateFromJson(char *json);

/* OLED 滚动显示任务（周期调用） */
void CabinetView_ScrollTask(void);

#endif