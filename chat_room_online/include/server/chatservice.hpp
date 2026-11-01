//这一层只负责业务层，也就是说将服务器的功能进行细分，这一层只负责处理业务逻辑，不负责网络传输，只负责连接之后的业务。
#ifndef CHATSERVICE_H
#define CHATSERVICE_H
//我们设计的时候，这一层是专门实现服务器的中转请求，这一层也是服务器的一个整体，其他的server文件都是为这一个文件进行服务的。
#include <muduo/net/Callbacks.h>
#include<muduo/net/TcpConnection.h>
#include"public.hpp"
#include"redis/redis.hpp"
#include<string>
#include<unordered_map>
#include<functional>
#include<mutex>

//函数包装器，也就是通用函数容器，能装下任何参数表和返回值匹配的可调用对象--包括普通函数，lambda表达式，函数对象等
using MsgHandler = std::function<void(const muduo::net::TcpConnectionPtr &conn,const Message &m)>;

class ChatService{
    private:
        ChatService();
        void init_handlers();//这个函数就是将我们所需要的大量方法注册进handlers。

        //序列化加上分隔符
        //只起到发送作用，不负责处理业务的功能，是业务层唯一的发送接口
        void send_message(const muduo::net::TcpConnectionPtr &conn,const Message &m);
        //统一的错误回包
        //发送接口由send_message进行发送
        void send_error(const muduo::net::TcpConnectionPtr &conn, const Message &req,int code);

        //统一推送出口：本地在线表命中就直接投递，本进程没有才走redis，redis无人订阅则写离线表
        void send_or_offline(int to_id,const ChatPush &push);
        //redis接收线程的回调，只做转投到目标连接所属的IO线程，不碰数据库
        void on_redis_message(int channel,const std::string &msg);
        //反查这条连接对应的用户id，未登录返回0（登录成功时用conn->setContext()存进去）
        int user_id_of(const muduo::net::TcpConnectionPtr &conn) const;

        void handle_register(const muduo::net::TcpConnectionPtr &conn,const Message &m);
    
        void handle_login(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_logout(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_deregister(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_change_passwd(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_add_friend(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_delete_friend(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_get_friend_list(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_create_group(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_join_group(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_exit_group(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_modify_group(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_transfer_group(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_get_group_list(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_get_group_member(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_chat_private(const muduo::net::TcpConnectionPtr &conn,const Message &m);

        void handle_chat_group(const muduo::net::TcpConnectionPtr &conn,const Message &m);
        
        //本质上是将大量的实现方法通过编号来进行调用，简化了我们需要记住每一种方法要调用什么函数的困扰，尽管我们仍需要知道每个函数的作用是什么
        //更准确的说，这是一个方法集合。
        std::unordered_map<int, MsgHandler> handlers_;

        //搭建用户编号与具体连接的索引，其本质是一张索引表，通过用户id与具体的通信对象连接起来。
        std::unordered_map<int,muduo::net::TcpConnectionPtr> online_;

        //redis只做跨进程中转：本进程查不到连接时，才通过它投给别的进程
        Redis_client redis_;

        std::mutex mutex_;
    public:
        static ChatService *instance();
        
        //网络层切除一条完整消息后调用 （不含结尾的'\n'）
        void handle_message(const muduo::net::TcpConnectionPtr& conn,const std::string &line);
        //连接断开时清理在线表，也就是网络断开后的业务层面的清理，不是网络层，是业务层的清理
        void client_close(const muduo::net::TcpConnectionPtr&conn);

        //退出前停掉 redis 接收线程
        void stop();
     

    

};
#endif
