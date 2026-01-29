#include "tcp.h"
#include "usart.h"
#include "esp8266.h"
#include "stdio.h"
#include "string.h"
#include "main.h"

volatile uint8_t TcpClosedFlag = 0;

void ESP8266_STA_TCPClient_Test(void)
{
    u8 res;
    char str[100] = {0};
    ESP8266_AT_Test();
    printf("正在配置ESP8266\r\n");
    ESP8266_Net_Mode_Choose(STA);
    while(!ESP8266_JoinAP(User_ESP8266_SSID, User_ESP8266_PWD));
    ESP8266_Enable_MultipleId(DISABLE);
    while(!ESP8266_Link_Server(enumTCP, User_ESP8266_TCPServer_IP, User_ESP8266_TCPServer_PORT, Single_ID_0));
    while(!ESP8266_UnvarnishSend());
    printf("\r\n配置完成");

    while (1)
    {       
        sprintf(str,"杭州光子物联科技有限公司"); //格式化发送字符串到TCP服务器
        ESP8266_SendString(ENABLE, str, 0, Single_ID_0);
        HAL_Delay(200);
        Uart_RecvFlag(); //接收数据
        if(TcpClosedFlag) //判断是否失去连接
        {
            ESP8266_ExitUnvarnishSend(); //退出透传模式
            do
            {
                res = ESP8266_Get_LinkStatus(); //获取连接状态
            }   
            while(!res);

            if(res == 4) //确认失去连接，重连
            {               
                while (!ESP8266_JoinAP(User_ESP8266_SSID, User_ESP8266_PWD));
                while (!ESP8266_Link_Server(enumTCP, User_ESP8266_TCPServer_IP, User_ESP8266_TCPServer_PORT, Single_ID_0));        
            } 
            while(!ESP8266_UnvarnishSend());                    
        }
    }   
}

// 新增 TCP_ProcessData 实现
void TCP_ProcessData(uint8_t *data, uint16_t len)
{
    // 简单打印接收到的数据，可根据需要替换为业务处理逻辑
    printf("TCP Received (%d bytes): %.*s\n", len, len, data);
}