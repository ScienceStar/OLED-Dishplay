# TCP网络调试助手配置指南

## 📝 概述

此文档说明如何配置网络调试助手与STM32F103 + ESP8266设备进行TCP通信。

---

## 🔧 设备端配置 (STM32F103)

### 1. TCP服务器配置 (Inc/tcp.h)

修改以下参数以匹配你的网络环境：

```c
#define WIFI_SSID       "TP-LINK_F643"      // 改为你的WiFi热点名称
#define WIFI_PWD        "Vast314&Star159$"  // 改为你的WiFi密码

#define TCP_SERVER_IP   "192.168.0.10"      // 改为你的电脑/服务器IP
#define TCP_SERVER_PORT 8888                // 改为监听端口 (建议 8888)
```

### 2. 获取电脑IP地址

#### macOS:
```bash
# 查看本地IP
ifconfig | grep "inet " | grep -v 127.0.0.1
# 例如输出: 192.168.0.10
```

#### Windows:
```bash
ipconfig
# 查找 "本地连接" 或 "以太网适配器" 的 IPv4 地址
# 例如: 192.168.0.10
```

#### Linux:
```bash
hostname -I
# 或
ip addr show
```

### 3. WiFi热点配置

- ESP8266需要连接到**2.4GHz WiFi**（不支持5GHz）
- 建议使用手机热点或路由器的2.4GHz频段
- WiFi密码中避免特殊字符（如 `&` `$` 等可能需要转义）

---

## 🌐 电脑端配置 (网络调试助手)

### 方案1: 使用专业TCP调试软件

#### A. NetAssist (Windows)
1. 下载: http://www.wjgnet.com/netassist.html
2. 打开应用
3. 配置:
   - **协议**: TCP Server
   - **本地端口**: 8888
   - **监听地址**: 0.0.0.0 (监听所有网卡)
4. 点击"监听"按钮

#### B. Hercules (跨平台)
1. 下载: https://www.hw-group.com/products/hercules/index_en.html
2. 打开应用
3. 选择 "TCP Server" 标签页
4. 配置:
   - **端口**: 8888
   - **监听**: 点击启用

#### C. MobaXterm (Windows/Linux)
1. 下载: https://mobaxterm.mobatek.net/
2. 点击 "Sessions" → "New session"
3. 选择 "Network" → "TCP"
4. 配置:
   - **Listening port**: 8888

### 方案2: 使用Python快速测试

创建文件 `tcp_server.py`:

```python
#!/usr/bin/env python3
import socket
import threading

def handle_client(conn, addr):
    print(f"[连接] 客户端: {addr}")
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                break
            print(f"[接收] {data.decode('utf-8', errors='ignore')}")
    except Exception as e:
        print(f"[错误] {e}")
    finally:
        conn.close()
        print(f"[断开] 客户端: {addr}")

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(('0.0.0.0', 8888))
server.listen(5)

print("=" * 50)
print("TCP服务器启动")
print("监听端口: 8888")
print("等待客户端连接...")
print("=" * 50)

try:
    while True:
        conn, addr = server.accept()
        thread = threading.Thread(target=handle_client, args=(conn, addr))
        thread.daemon = True
        thread.start()
except KeyboardInterrupt:
    print("\n[停止] 服务器关闭")
finally:
    server.close()
```

运行:
```bash
python3 tcp_server.py
```

### 方案3: 使用nc (macOS/Linux)

```bash
# 简单监听
nc -l -p 8888

# 或使用ncat (更兼容)
ncat -l 127.0.0.1 8888
```

---

## 🔍 工作流程验证

### 步骤1: 启动电脑端TCP服务器

```
[TCP Server] 已启动在 0.0.0.0:8888
等待连接...
```

### 步骤2: 烧写设备

```bash
# 使用EIDE烧写程序到STM32F103
EIDE → Build and Flash
```

### 步骤3: 检查OLED显示

设备启动后OLED屏幕应显示:
- **左侧**: Elon Musk传记文本 (每秒更新)
- **右上角**:
  - `WiFi: X` → WiFi未连接
  - `WiFi: o` → WiFi弱信号
  - `WiFi: *` → WiFi中等信号
  - `WiFi:**` → WiFi强信号
  - `TCP: xx` → TCP未连接
  - `TCP: OK` → TCP已连接

### 步骤4: 检查TCP服务器

设备正常工作时，TCP服务器应该看到:

```
[连接] 客户端: 192.168.0.100:12345
[接收] Device Online
[接收] Device Online
[接收] Device Online
...
```

---

## 🐛 常见问题排查

### 问题1: ESP8266无法连接WiFi

**现象**: OLED显示 `WiFi: X`

**排查步骤**:
1. 检查SSID和密码是否正确 (Inc/tcp.h)
2. 确保WiFi是2.4GHz频段
3. 确保ESP8266天线连接良好
4. 检查UART连接 (PA2/PA3)
5. 尝试重启设备

### 问题2: WiFi连接成功，但TCP连不上

**现象**: OLED显示 `WiFi: *` 但 `TCP: xx`

**排查步骤**:
1. 检查TCP服务器IP地址是否正确
   ```bash
   # macOS/Linux:
   ifconfig | grep "inet "
   ```
2. 确保电脑和STM32在同一网络
3. 检查TCP服务器是否真的在监听
4. 确保防火墙未阻止8888端口
5. 尝试改变端口号 (如9999)

### 问题3: TCP连接成功但收不到数据

**现象**: OLED显示 `TCP: OK`，但TCP服务器未收到数据

**排查步骤**:
1. 检查发送间隔是否太短
2. 确保ESP8266进入透明传输模式成功
3. 尝试使用网络分析工具（如Wireshark）抓包检查
4. 检查串口通信是否正常

### 问题4: 频繁连接/断开

**现象**: TCP连接状态不稳定

**排查步骤**:
1. 检查网络连接质量 (信号强度)
2. 查看tcp.c中的重连逻辑 (30秒自动重连)
3. 增加超时时间或添加更多重试
4. 检查ESP8266模块是否过热

---

## 📡 TCP数据格式

### 设备发送格式

设备每2秒发送一次:
```
Device Online\r\n
```

### 自定义发送数据

修改 main.c 中的发送部分:

```c
/* 找到这一行 */
ESP8266_SendString("Device Online\r\n");

/* 改为: */
ESP8266_SendString("Custom Message\r\n");

/* 或使用sprintf拼接 */
char msg[100];
sprintf(msg, "WiFi RSSI: %d\r\n", WiFiRSSI);
ESP8266_SendString(msg);
```

---

## 🎯 高级配置

### 1. 修改发送间隔

编辑 main.c:

```c
/* 原代码: 2000ms */
if(now - last_tcp_send_tick >= 2000)

/* 改为1000ms (更频繁): */
if(now - last_tcp_send_tick >= 1000)

/* 或5000ms (更稀疏): */
if(now - last_tcp_send_tick >= 5000)
```

### 2. 修改LED动画周期

编辑 main.c:

```c
/* 原代码: 5000ms */
if(now - last_morse_tick >= 5000)

/* 改为: */
if(now - last_morse_tick >= 10000)  // 10秒发送一次
```

### 3. 添加心跳检测

修改 tcp.c 中的重连逻辑:

```c
if(TcpClosedFlag || (now - tcp_retry_tick > 30000))
{
    /* 30秒后自动重连 */
    tcp_state = TCP_IDLE;
}

/* 改为更激进的检测: */
if(TcpClosedFlag || (now - tcp_retry_tick > 10000))
{
    /* 10秒后重连 */
    tcp_state = TCP_IDLE;
}
```

---

## ✅ 最终检查清单

- [ ] WiFi SSID和密码正确
- [ ] 电脑IP地址正确输入
- [ ] TCP服务器端口是8888
- [ ] 电脑和设备在同一网络
- [ ] 防火墙未阻止8888端口
- [ ] ESP8266已正确烧写
- [ ] UART连接良好
- [ ] OLED屏幕显示正常
- [ ] LED呼吸灯闪烁正常

---

## 📞 遇到问题?

1. 检查编译是否成功 (无错误)
2. 确认烧写无误 (设备重启后OLED亮起)
3. 查看串口输出 (使用串口监控工具)
4. 逐一排查各个模块

---

## 🔗 相关资源

- [TCP通信问题排查](PROJECT_INTEGRATION_SUMMARY.md)
- [快速参考指南](QUICK_REFERENCE.md)
- [ESP8266 AT命令](Src/esp8266.c)
- [项目整合说明](PROJECT_INTEGRATION_SUMMARY.md)

---

**最后更新**: 2026-01-31  
**版本**: v1.1 - TCP增强版
