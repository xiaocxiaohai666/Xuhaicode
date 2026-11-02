#include"crypto.hpp"
#include"chatserver.hpp"
#include"chatservice.hpp"

#include<muduo/net/InetAddress.h>

#include<iostream>
#include<string>

using namespace std;
using namespace muduo::net;

namespace{
//单条消息在缓冲区里迟迟等不到换行时的上限，超过就判定为畸形报文并断开
const size_t kMaxBufferBytes = 1024 * 1024;
}

Chat_server::Chat_server(EventLoop *loop,const string &ip,uint16_t port,const string &nameArg,int threads)
    :server_(loop,InetAddress(ip,port),nameArg),loop_(loop){
    server_.setThreadNum(threads);

    //这两个回调由 muduo 在事件发生时调用，这里只做转发，业务全部交给 ChatService
    server_.setConnectionCallback(
        [this](const TcpConnectionPtr &conn){ on_connection(conn); });
    server_.setMessageCallback(
        [this](const TcpConnectionPtr &conn,Buffer *buf,muduo::Timestamp){ on_message(conn,buf,muduo::Timestamp()); });
}

Chat_server::~Chat_server() = default;

void Chat_server::start(){
    server_.start();
}

void Chat_server::on_connection(const TcpConnectionPtr &conn){
    if(conn->connected()){
        cout << conn->peerAddress().toIpPort() << " 已连接" << endl;
    }else{
        cout << conn->peerAddress().toIpPort() << " 已断开" << endl;
        //连接的清理属于业务层，网络层只负责通知
        ChatService::instance()->client_close(conn);
    }
}

void Chat_server::on_message(const TcpConnectionPtr &conn,Buffer *buf,muduo::Timestamp){
    //客户端固定以 '\n' 结尾，所以用 findEOL 而不是 findCRLF
    //拆包统一交给 wire::decode：明文模式按 '\n' 切，加密模式按长度头切
    size_t consumed = 0;
    string plain;
    while(wire::decode(buf->peek(),buf->readableBytes(),consumed,plain)){
        buf->retrieve(consumed);
        if(!plain.empty()){
            ChatService::instance()->handle_message(conn,plain);
        }
    }

    if(buf->readableBytes() > kMaxBufferBytes){
        cout << "报文过长，主动断开连接" << endl;
        conn->forceClose();
    }
}
