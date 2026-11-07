#ifndef SERVER_REDIS_REDIS_H
#define SERVER_REDIS_REDIS_H

#include<atomic>
#include<deque>
#include<functional>
#include<mutex>
#include<string>
#include<thread>
#include<set>

struct redisContext;//hiredis基于c写的官方结构体，代表一条到redis服务器的连接

//redis服务器这边不设计连接池，这是因为Redis的建连开销极小，并且Redis是长连接，常驻形态,没有设计池的必要
//况且我们redis服务器的一条连接可以监听所有的频道，这个监听频道的数量是没有限制的，况且hiredis并不是线程安全的，所以必须单线程独占
//所以管理这个redis的连接只能由一个线程去管理，其他线程不具备写和读的能力，连读的能力都没有，所以这里直接采用一条连接，一个线程专项管理，不需要用到池。

//在redis眼中，它的客户端就是服务器进程，一个进程有两条连接，分别是订阅以及发布，这两条连接
//Redis 连接数 = 服务器进程数 × 2
                //├─ 1 条 pub_ctx_：只发 PUBLISH
                //└─ 1 条 sub_ctx_：只收推送，独占一个接收线程


struct Options{
    std::string host = "127.0.0.1";
    int port = 6379;
    int connect_timeout_ms = 1000; //设置超时连接时长
    int poll_ms = 200;  //接收循环的唤醒粒度
    int retry_ms = 1000;//重连间隔
};

//按 key=value 格式读取 redis.conf 填充 Options
//path 为空时使用编译期注入的 conf 目录；文件打不开或缺少 host/port 时返回 false
bool load_options(Options &opt,const std::string &path = "");

class Redis_client{
    public:
        using Handler = std::function<void(int channel,const std::string &msg)>;

        explicit Redis_client(Options opt = Options());//禁止隐式转换
        ~Redis_client();
        Redis_client(const Redis_client & ) = delete;
        Redis_client &operator=(const Redis_client &) = delete;

        //用配置文件读出来的参数覆盖构造时的默认值，必须在 start() 之前调用
        void set_options(const Options &opt);

        //必须在 start() 之前调用一次
        void set_handler(Handler h);//注册回调函数的

        bool start();
        void stop();    //先 shutdown 打断 recv，再 join，最后 redisFree

        //返回订阅者数量，0 表示无人订阅（可用于判定离线）
        int publish(int channel,const std::string &msg);

        //只登记，立即返回；正真的命令是由接收线程发出的
        bool subscribe(int channel);
        bool unsubscribe(int channel);

        bool is_connected() const;

    private:
        bool open_conn();//创建这两个连接
        void close_conn();//关闭连接销毁句柄
        void recv_loop();//接收线程的主循环
        void flush_pending();//将队列中积压的提交和未提交命令正真的发送出去
        bool send_only(redisContext *ctx,const std::string &cmd);
        void reconnect();//断线后重连，并补充做恢复工作
        void resubscribe_all();    //重连后按台账补订所有频道

        Options opt_;
        redisContext *pub_ctx_ = nullptr; //只发，由工作线程执行
        redisContext *sub_ctx_ = nullptr;//只收，只有接收线程碰，专属于redis的线程
        std::thread recv_thread_;         //独占 sub_ctx_ 的接收线程
        
        std::atomic<bool> running_{false};
        std::atomic<bool> connected_{false};

        std::mutex pub_mtx_;
        std::mutex pend_mtx_;
        std::deque<std::string> pending_;
        std::set<int> channels_; //订阅台账，解决重复订阅的去重问题

        Handler handler_; //start之后只读，是保存回调线程的成员变量
};



#endif
