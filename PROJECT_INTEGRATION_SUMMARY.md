# OLED显示屏项目整合总结

## 项目概述

本项目是基于 STM32F103C8TX 微控制器的综合物联网应用，集成了以下功能：

### 1. **OLED显示屏功能** 📺
- **驱动芯片**: SSD1306 OLED显示器 (128×64 分辨率)
- **通信方式**: SPI 接口
- **功能特性**:
  - 显示个人传记信息 (Elon Musk 生平)
  - 每秒自动滚动显示不同行的文本
  - 支持显示字符、数字、汉字和BMP图片

### 2. **LED呼吸灯功能** 💡
- **LED硬件**: 
  - LED1: PC13 (WiFi信号强度指示)
  - LED2(呼吸灯): PB4 (摩尔斯电码显示)
- **摩尔斯电码**:
  - 显示"SOS"信号，每5秒循环一次
  - LED通过呼吸灯效果表现点(.)和划(-)
  - 支持数字、字母和空格的摩尔斯编码

### 3. **ESP8266 WiFi & TCP通信** 🌐
- **模块**: ESP8266 WiFi模块
- **UART通信**: USART2 (波特率: 115200)
- **功能实现**:
  - AT命令控制
  - WiFi模式配置 (STA/AP/STA+AP)
  - 自动连接指定的WiFi AP
  - RSSI信号强度获取
  - TCP透明传输模式
  - 周期性数据发送

## 代码架构分析

### 核心文件结构

```
OLED-Dishplay/
├── Src/                          # 源代码目录
│   ├── main.c                    # 主程序，包含系统初始化和主循环
│   ├── oled.c                    # OLED显示驱动
│   ├── esp8266.c                 # ESP8266 WiFi通信驱动
│   ├── tcp.c                     # TCP状态机管理
│   ├── usart.c                   # UART配置
│   ├── gpio.c                    # GPIO初始化
│   ├── i2c.c                     # I2C外设配置
│   └── sys.c                     # 系统配置
├── Inc/                          # 头文件目录
│   ├── main.h                    # 全局声明
│   ├── oled.h                    # OLED接口定义
│   ├── esp8266.h                 # ESP8266 API
│   ├── tcp.h                     # TCP配置和任务
│   ├── oledfont.h                # OLED字体数据
│   └── 其他peripheral头文件
├── Drivers/                      # STM32 HAL库
│   ├── CMSIS/                    # Cortex-M3核心
│   └── STM32F1xx_HAL_Driver/     # STM32F1硬件抽象层
└── build/                        # 编译输出目录
```

## 功能集成情况

### ✅ 已完全集成的功能

| 功能模块 | 状态 | 关键文件 | 说明 |
|---------|------|--------|------|
| OLED显示 | ✅ | oled.c, oled.h | 完整的SSD1306驱动，支持文字/数字/汉字显示 |
| 摩尔斯电码 | ✅ | main.c | SOS信号每5秒发送一次，LED呼吸效果 |
| LED控制 | ✅ | main.c, gpio.c | PC13和PB4双LED，WiFi状态和RSSI显示 |
| ESP8266驱动 | ✅ | esp8266.c, esp8266.h | AT命令集完整实现 |
| WiFi连接 | ✅ | tcp.c, esp8266.c | 自动连接指定SSID和密码 |
| TCP通信 | ✅ | tcp.c, esp8266.c | 透明模式发送数据 |
| UART通信 | ✅ | usart.c | 中断接收，支持1024字节缓冲 |
| 系统时钟 | ✅ | main.c | 72MHz PLL时钟配置 |

### 📊 主循环任务分配

```c
主循环执行以下周期任务:
├── 1000ms: OLED显示更新 (显示下一行传记)
├── 5000ms: 摩尔斯电码发送 (SOS)
├── 100ms:  LED WiFi状态更新
├── 1000ms: TCP数据发送 (WiFi连接时)
└── 即时:   UART数据接收处理
```

## 最近的代码改进

### 修复项目 (2026-01-31)

1. **ESP8266 API完善**
   - ✅ 在 `esp8266.h` 中添加 `ESP8266_SendString()` 函数声明
   - ✅ 修正 `esp8266.c` 中使用正确的 `ESP8266_UART` 宏而非硬编码 `huart2`

2. **全局变量导出**
   - ✅ 在 `main.h` 中导出 WiFi 相关变量:
     - `extern uint8_t WiFiStatus`
     - `extern int8_t WiFiRSSI`
     - `extern volatile uint8_t TcpClosedFlag`

3. **函数声明规范化**
   - ✅ 在 `main.h` 中添加 `esp8266_led_update()` 函数声明

## 编译状态

### ✅ 编译成功

```
编译输出:
- ELF文件: 145KB
- HEX文件: 33KB
- FLASH使用: 12020 B / 64 KB (18.34%)
- RAM使用: 4504 B / 20 KB (21.99%)
```

**编译工具**: ARM GNU Toolchain 15.2.1
**目标架构**: STM32F103C8TX (ARM Cortex-M3)
**优化等级**: Debug配置

### 🔍 编译警告 (可忽略)

仅有标准的链接器警告，关于未实现的标准C库I/O函数 (`_close`, `_lseek`, `_read`, `_write`)，这在嵌入式项目中是正常现象。

## 硬件连接配置

### GPIO映射表

| 功能 | GPIO端口 | 引脚号 | 用途 |
|------|---------|--------|------|
| LED (WiFi指示) | GPIOC | 13 | WiFi信号强度指示 |
| LED (呼吸灯) | GPIOB | 4 | 摩尔斯电码显示 |
| OLED_CLK | GPIOB | 8 | OLED时钟 |
| OLED_DIN | GPIOB | 9 | OLED数据 |
| OLED_DC | GPIOB | 12 | OLED数据/命令 |
| OLED_CS | GPIOB | 13 | OLED片选 |
| USART2_TX | GPIOA | 2 | ESP8266通信 |
| USART2_RX | GPIOA | 3 | ESP8266通信 |
| I2C1_SCL | GPIOB | 6 | 预留I2C接口 |
| I2C1_SDA | GPIOB | 7 | 预留I2C接口 |

### WiFi配置

在 `Inc/tcp.h` 中配置:
```c
#define WIFI_SSID   "TP-LINK_F643"         // 2.4GHz WiFi SSID
#define WIFI_PWD    "Vast314&Star159$"     // WiFi密码
#define TCP_SERVER_IP   "192.168.0.3"      // 目标服务器IP
#define TCP_SERVER_PORT 8888               // 目标服务器端口
```

## 功能工作流程

### 系统启动序列
```
1. HAL初始化 → 系统时钟配置 (72MHz)
2. GPIO初始化 → LED和OLED引脚配置
3. UART初始化 → ESP8266通信设置
4. OLED初始化 → 屏幕清空
5. ESP8266初始化 → 延迟1000ms稳定
6. 启用UART中断接收
7. 进入主循环
```

### 主循环执行流
```
┌─ 每1000ms: 更新OLED显示 (滚动传记文本)
├─ 每5000ms: 发送摩尔斯"SOS"信号 (LED呼吸灯)
├─ 每100ms:  更新WiFi状态LED指示
├─ 每1000ms: 如果WiFi连接，通过TCP发送"Hello Server"
└─ 即时处理: UART中断接收ESP8266数据
   ├─ 检测WIFI CONNECTED/DISCONNECTED
   ├─ 解析RSSI信号强度
   └─ 检测TCP连接状态
```

### WiFi TCP连接流程 (tcp.c)
```
TCP_IDLE
  ↓ (测试AT命令成功)
TCP_WIFI (配置STA模式，连接WiFi)
  ↓ (WiFi连接成功)
TCP_CONNECT (连接TCP服务器，进入透明模式)
  ↓ (TCP连接成功)
TCP_WORK (周期性发送"杭州光子物联科技有限公司"数据)
```

## 扩展功能建议

1. **OLED显示优化**
   - 添加中文显示支持
   - 显示实时WiFi信号强度条

2. **TCP功能增强**
   - 实现接收数据处理 (TCP_ProcessData 函数)
   - 添加重连机制
   - 支持多个数据帧

3. **LED效果增强**
   - 不同WiFi强度显示不同闪烁模式
   - PWM调节呼吸灯亮度

4. **系统监控**
   - 添加看门狗(WDG)
   - 内存使用监控
   - 错误日志记录

## 编译和烧写指令

### 使用EIDE IDE编译
```bash
# 构建项目
EIDE → Build

# 烧写到设备
EIDE → Upload to Device

# 完整构建和烧写
EIDE → Build and Flash
```

### 命令行编译 (需要arm-none-eabi工具链)
```bash
cd /Users/pr/Public/workspace/MCU/OLED-Dishplay
# 编译由EIDE管理，生成文件在 build/Debug/ 目录
```

### 烧写工具
- JLink调试器或 STLink v2
- 使用 JFlash 或 st-flash 工具

## 项目状态: ✅ 就绪待烧写

所有代码已完全整合，编译无错误，可直接烧写到STM32F103C8TX设备。

---

**最后更新**: 2026年1月31日  
**维护者**: MCU开发团队  
**项目类型**: STM32F1 嵌入式系统  
**编译状态**: ✅ 成功
