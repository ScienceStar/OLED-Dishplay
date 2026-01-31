# ⚡ 快速启动 - 5分钟上手

## 🎯 目标

在5分钟内让TCP网络调试助手成功监听到STM32设备的数据。

---

## ⏱️ 步骤 (1-5分钟)

### ✅ 第1步: 获取电脑IP (1分钟)

**macOS/Linux**:
```bash
ifconfig | grep "inet " | grep -v 127.0.0.1
# 复制类似 192.168.0.10 的地址
```

**Windows**:
```bash
ipconfig
# 找到 "本地连接" 的IPv4地址，如 192.168.0.10
```

### ✅ 第2步: 修改设备配置 (1分钟)

打开 `Inc/tcp.h`:

```c
#define TCP_SERVER_IP   "192.168.0.10"      // 粘贴你的电脑IP
#define TCP_SERVER_PORT 8888                // 保持不变
```

### ✅ 第3步: 重新编译烧写 (2分钟)

在EIDE中:
```
Build and Flash
```

### ✅ 第4步: 启动TCP服务器 (1分钟)

选择一种方法:

**方法A - Python (最简单)**
```bash
# 创建 server.py 文件，内容如下:
python3 << 'EOF'
import socket, threading
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('0.0.0.0', 8888))
server.listen(5)
print("TCP Server 监听 0.0.0.0:8888")
while True:
    conn, addr = server.accept()
    print(f"[连接] {addr}")
    while (data := conn.recv(1024)):
        print(f"[接收] {data.decode()}", end='')
    conn.close()
EOF
```

**方法B - nc (macOS/Linux)**
```bash
nc -l -p 8888
```

### ✅ 第5步: 查看结果

**OLED屏幕显示**:
```
左侧: Elon Musk传记文本
右上: WiFi:** TCP:OK
```

**TCP服务器输出**:
```
TCP Server 监听 0.0.0.0:8888
[连接] 192.168.0.100:12345
[接收] Device Online
[接收] Device Online
[接收] Device Online
...
```

---

## 🎉 成功!

如果看到上面的输出，说明一切正常！

---

## 🐛 不行？快速排查

| 问题 | 快速解决 |
|------|---------|
| 看不到[连接] | IP地址错误，改为正确的电脑IP |
| WiFi显示 X | WiFi未连接，检查热点名称和密码 |
| TCP显示 xx | TCP未连接，检查服务器是否真的在监听 |
| 收不到数据 | 防火墙阻止，关闭防火墙或放行8888端口 |

---

## 📖 需要更多帮助?

查看详细文档: [TCP_NETWORK_DEBUG_GUIDE.md](TCP_NETWORK_DEBUG_GUIDE.md)

---

**准备好了吗？开始吧！** 🚀
