//这一层是负责网络传输层，也就是说将服务器的功能进行细分，
//这一层只负责服务器与客户端的连接与数据传输，不管用户具体的业务。
#ifndef CHATSERVER_H
#define CHATSERVER_H
//这是muduo库的tcp服务器类
#include<muduo/net/TcpServer.h>
//里面包含了：
//Acceptor(持有监听socket，负责accept()新连接)
//EventLoopThreadPool 包含vector存储的固定式线程池组件，里面的每个子线程都有一个轮询调度的i/o。
//EventLoop* loop_ 主线程的事件循环，只管accept，不处理请求
#include<muduo/net/EventLoop.h>
#include<string>
#include<cstdint>
//事件循环，muduo的核心，一个EventLoop绑定一个线程，内部跑epoll_wait。

class Chat_server{
    private:
    //上报链接相关信息的回调函数
    //回调函数的意思是：你不是主动去调用它，而是muduo在特定事件发生时替你调用它
    //里面的参数指的是之管理一个连接对象的智能指针
        //负责数据的连接以及断开
        void on_connection(const muduo::net::TcpConnectionPtr &);
    //智能指针加上缓冲区和时间戳
        //负责数据的到达与处理
        void on_message(const muduo::net::TcpConnectionPtr &, muduo::net::Buffer *, muduo::Timestamp);

    muduo::net::TcpServer server_;  //组合muduo库中的tcp服务器类对象
    muduo::net::EventLoop * loop_;  //事件循环对象，也就是之前cs项目事件轮询

    public:
        Chat_server(muduo::net::EventLoop * loop, const std::string &ip, uint16_t port, const std::string &nameArg, int threads);
        ~Chat_server();

        //启动服务器
        void start();
    };

#endif