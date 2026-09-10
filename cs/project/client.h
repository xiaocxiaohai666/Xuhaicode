#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <sys/epoll.h>
#include <pthread.h>
#include<jsoncpp/json/json.h>
/*一、整段通信流程（JSON 协议收发全过程）
发送端
先把要发的业务数据（账号、密码、状态、消息等）
组装成标准 JSON 结构 {"status":"ok","name":"xxx"}
把这个 JSON 对象 转成普通字符串
通过网络套接字（TCP/UDP）以字节流 / 字符串形式发给对方
接收端（就是你上面那段代码）
从网络读到的就是一大段普通字符串 buff_status
用 Json::Reader.parse() 把 JSON 字符串反向解析成 Json::Value 对象
然后就能 resval["status"] 取字段、判断逻辑
*/
using namespace std;

class Client
{
public:
    Client()
    {
        ips = "127.0.0.1";
        port = 6000;
        sockfd = -1;
        runing = true;
        dl_status = false;
    }

    ~Client()
    {
        close(sockfd);
    }

    bool Connect();
    void Run();

private:
    void Show_Menu();
    void User_Register();
    void User_Login();
    void Show_Ticket();//显示可预约的信息
    void Yd_Ticket();//预定
    void Show_My_Ticket();//查看我自己的预约信息
    void Qx_Ticket();//取消票信息。
private:
    int sockfd; // 套接字
    string ips; // 服务器IP地址
    short port; // 服务器端口

    bool runing;
    bool dl_status;

    int User_Op;//记录用户选择的操作编号
    string user_name;//用户名
    string user_tel;//用户手机号码

};