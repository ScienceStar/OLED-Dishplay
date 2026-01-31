# 快速参考指南 - OLED WiFi LED项目

## 🎯 项目概览 (30秒速览)

| 项目名称 | OLED显示屏+LED呼吸灯+WiFi通信集成系统 |
|---------|--------------------------------------|
| **芯片** | STM32F103C8TX (ARM Cortex-M3) |
| **主要功能** | OLED滚动显示 + 摩尔斯灯 + WiFi TCP |
| **编译状态** | ✅ 成功 (无错误) |
| **烧写状态** | ✅ 就绪 |
| **最后更新** | 2026-01-31 |

---

## 📂 关键文件位置

| 功能 | 源文件 | 头文件 |
|------|--------|--------|
| **OLED驱动** | `Src/oled.c` | `Inc/oled.h` |
| **WiFi通信** | `Src/esp8266.c` | `Inc/esp8266.h` |
| **TCP任务** | `Src/tcp.c` | `Inc/tcp.h` |
| **主程序** | `Src/main.c` | `Inc/main.h` |
| **字体数据** | - | `Inc/oledfont.h` |

---

## ⚙️ 系统配置

### WiFi配置 (Inc/tcp.h)
```c
#define WIFI_SSID       "TP-LINK_F643"          // 修改为你的WiFi名称
#define WIFI_PWD        "Vast314&Star159$"      // 修改为你的WiFi密码
#define TCP_SERVER_IP   "192.168.0.3"           // 修改为目标服务器IP
#define TCP_SERVER_PORT 8888                    // 修改为目标端口
```

### 硬件引脚映射

```
LED指示灯:
├─ WiFi状态(PC13): 无信号熄灭, 有信号闪烁
└─ 呼吸灯(PB4): 摩尔斯"SOS"动画

OLED屏 (SPI接口):
├─ CLK(PB8), DIN(PB9), DC(PB12), CS(PB13)
└─ 显示分辨率: 128×64 像素

通信模块:
├─ ESP8266 UART: USART2 (PA2/PA3), 115200bps
└─ I2C预留: I2C1 (PB6/PB7)
```

---

## 🔄 主程序流程

### 启动阶段 (main函数)
```
初始化系统
  ├─ HAL库初始化
  ├─ 系统时钟 → 72MHz
  ├─ GPIO初始化 → LED和OLED引脚
  ├─ UART初始化 → ESP8266通信
  ├─ OLED初始化 → 屏幕清空
  ├─ ESP8266初始化 → 延迟1秒
  └─ 启用UART中断接收
```

### 主循环任务 (while循环)
```
持续执行:

【每1000ms】
  OLED显示滚动传记文本
  current_index++

【每5000ms】
  摩尔斯电码: morse_send("SOS")
  
【每100ms】
  ESP8266 LED状态更新
  
【每1000ms】
  如果WiFi连接: 发送TCP数据
  
【即时处理】
  接收UART数据 → 更新WiFi/TCP状态
```

---

## 🔧 常用函数速查

### OLED操作
```c
OLED_Init();                              // 初始化OLED
OLED_Clear();                             // 清屏
OLED_ShowString(x, y, (uint8_t*)text);  // 显示文本
OLED_ShowNum(x, y, num, len, size);     // 显示数字
OLED_ShowCHinese(x, y, no);             // 显示汉字
```

### ESP8266操作
```c
ESP8266_Init();                           // 初始化模块
ESP8266_AT_Test();                        // 测试AT命令
ESP8266_SetMode(STA);                     // 配置WiFi模式
ESP8266_JoinAP(ssid, pwd);                // 连接WiFi
ESP8266_TCP_Connect(ip, port);            // TCP连接
ESP8266_TCP_Send(data);                   // 发送数据
ESP8266_SendString("message");            // 发送字符串
```

### LED操作
```c
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    // LED亮
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  // LED灭
breathe_led_smooth(duration);                            // LED呼吸效果
morse_send("SOS");                                       // 摩尔斯电码
```

---

## 📡 WiFi连接工作流

```
程序启动
  ↓
[TCP_IDLE] 测试AT命令
  ↓ ✓
[TCP_WIFI] 设置STA模式 + 连接WiFi
  ↓ ✓ (WiFi已连接)
[TCP_CONNECT] TCP连接服务器 + 进入透明模式
  ↓ ✓ (TCP已连接)
[TCP_WORK] 周期性发送数据到服务器
  ↓
监控WiFi/TCP状态，自动重连
```

---

## 🐛 常见问题排查

### 问题1: OLED不显示
```
排查步骤:
1. 检查GPIO初始化 (Src/oled.c OLED_Init)
2. 检查SPI引脚: PB8(CLK), PB9(DIN), PB12(DC), PB13(CS)
3. 检查OLED屏幕电源
4. 运行 OLED_ShowString() 测试
```

### 问题2: ESP8266无法连接WiFi
```
排查步骤:
1. 检查UART波特率: 115200
2. 检查SSID和密码配置 (Inc/tcp.h)
3. 尝试 AT+RST 硬重启
4. 检查模块天线连接
5. 查看UART接收的响应
```

### 问题3: LED不闪烁
```
排查步骤:
1. 检查LED引脚: PC13(指示) 和 PB4(呼吸)
2. 检查 morse_send() 是否被调用 (每5秒)
3. 确保 breathe_led_smooth() 函数正常运行
4. 检查GPIO输出是否启用
```

---

## 📊 编译信息

```
构建配置: Debug
优化级别: -O2
目标架构: ARM Cortex-M3
工具链版本: ARM GCC 15.2.1

内存分配:
├─ FLASH: 12020 B (18.34%)  ← 程序代码
├─ RAM:    4504 B (21.99%)  ← 运行数据
└─ 余量足够进行功能扩展 ✓
```

---

## 🚀 快速烧写

### 方法1: 使用EIDE IDE
```
1. 打开项目
2. 菜单 → Build and Flash
3. 选择烧写工具(JLink或STLink)
4. 等待烧写完成
```

### 方法2: 命令行 (需st-flash)
```bash
# 编译
arm-none-eabi-gcc ... -o build/app.elf

# 烧写到设备
st-flash write build/app.bin 0x8000000
```

---

## 📝 实用代码片段

### 显示WiFi连接状态
```c
if(WiFiStatus == 1) {
    if(WiFiRSSI >= -50) {
        OLED_ShowString(0, 0, (uint8_t*)"WiFi: Excellent");
    } else if(WiFiRSSI >= -70) {
        OLED_ShowString(0, 0, (uint8_t*)"WiFi: Good");
    } else {
        OLED_ShowString(0, 0, (uint8_t*)"WiFi: Weak");
    }
}
```

### 自定义OLED显示
```c
OLED_Clear();
OLED_ShowString(0, 0, (uint8_t*)"Elon Musk");
OLED_ShowString(0, 2, (uint8_t*)"Born: 1971");
OLED_ShowString(0, 4, (uint8_t*)"Location: Earth");
```

### 发送自定义TCP消息
```c
char msg[100];
sprintf(msg, "WiFi RSSI: %d\r\n", WiFiRSSI);
ESP8266_SendString(msg);
```

---

## ✅ 修改日志

| 日期 | 修改 | 状态 |
|------|------|------|
| 2026-01-31 | 完成ESP8266功能集成 | ✅ |
| 2026-01-31 | 修复函数声明和变量导出 | ✅ |
| 2026-01-31 | 验证编译无错误 | ✅ |
| 2026-01-31 | 生成文档 | ✅ |

---

## 📚 相关文档

- 完整集成说明: [PROJECT_INTEGRATION_SUMMARY.md](PROJECT_INTEGRATION_SUMMARY.md)
- 修改记录: [MODIFICATION_RECORD.md](MODIFICATION_RECORD.md)
- STM32F1 数据手册: `/Drivers/`
- HAL库文档: `/Drivers/STM32F1xx_HAL_Driver/`

---

**提示**: 遇到问题?
1. 查看 compiler.log 检查编译错误
2. 检查硬件连接和配置
3. 参考 PROJECT_INTEGRATION_SUMMARY.md 了解详细信息
4. 查看源代码注释获取实现细节

**项目状态**: 🟢 绿灯 - 就绪待烧写!
