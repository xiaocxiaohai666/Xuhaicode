#include"redis.hpp"

#include<hiredis/hiredis.h>

#include<sys/select.h>
#include<sys/socket.h>
#include<unistd.h>

#include<cerrno>
#include<chrono>
#include<cstdlib>
#include<fstream>
#include<iostream>

using namespace std;

//没有经过 CMake 注入时（例如直接 g++ 编译单文件）退回当前目录
#ifndef SERVER_CONF_DIR
#define SERVER_CONF_DIR "."
#endif

bool load_options(Options &opt,const std::string &path){
    const std::string file = path.empty() ? std::string(SERVER_CONF_DIR "/redis.conf") : path;

    std::ifstream in(file);
    if(!in.is_open()){
        std::cout << "打开 redis 配置失败: " << file << std::endl;
        return false;
    }

    std::string line;
    int num = 0;
    while(std::getline(in,line)){
        ++num;
        //兼容 Windows 换行：getline 只去掉 \n，\r 要单独处理
        if(!line.empty() && line.back() == '\r'){
            line.pop_back();
        }
        if(line.empty() || line[0] == '#'){
            continue;
        }

        const std::string::size_type pos = line.find('=');
        if(pos == std::string::npos){
            std::cout << file << " 第 " << num << " 行缺少等号" << std::endl;
            return false;
        }

        const std::string key = line.substr(0,pos);
        const std::string value = line.substr(pos + 1);

        if(key == "host"){
            opt.host = value;
        }else if(key == "port"){
            opt.port = std::atoi(value.c_str());
        }else if(key == "connect_timeout_ms"){
            opt.connect_timeout_ms = std::atoi(value.c_str());
        }else if(key == "poll_ms"){
            opt.poll_ms = std::atoi(value.c_str());
        }else if(key == "retry_ms"){
            opt.retry_ms = std::atoi(value.c_str());
        }else{
            std::cout << "未知配置项: " << key << "，位于 " << file << " 第 " << num << " 行" << std::endl;
        }
    }

    if(opt.host.empty() || opt.port <= 0){
        std::cout << file << " 的 host / port 不合法" << std::endl;
        return false;
    }
    if(opt.connect_timeout_ms <= 0 || opt.poll_ms <= 0 || opt.retry_ms <= 0){
        std::cout << file << " 的超时/轮询参数必须为正整数" << std::endl;
        return false;
    }
    return true;
}

Redis_client::Redis_client(Options opt):opt_(opt){}

Redis_client::~Redis_client(){
    stop();
}

void Redis_client::set_options(const Options &opt){
    if(running_){
        cout << "redis 已启动，忽略运行期修改配置" << endl;
        return;
    }
    opt_ = opt;
}

void Redis_client::set_handler(Handler h){
    handler_ = std::move(h);
}

bool Redis_client::open_conn(){
    timeval tv{};
    tv.tv_sec = opt_.connect_timeout_ms / 1000;
    tv.tv_usec = (opt_.connect_timeout_ms % 1000) * 1000;

    pub_ctx_ = redisConnectWithTimeout(opt_.host.c_str(),opt_.port,tv);
    //连接失败时 redisConnect 也返回非空 ctx，必须看 err 字段
    if(pub_ctx_ == nullptr || pub_ctx_->err != 0){
        cout << "redis 发布连接失败: " << (pub_ctx_ ? pub_ctx_->errstr : "ctx 为空") << endl;
        close_conn();
        return false;
    }

    sub_ctx_ = redisConnectWithTimeout(opt_.host.c_str(),opt_.port,tv);
    if(sub_ctx_ == nullptr || sub_ctx_->err != 0){
        cout << "redis 订阅连接失败: " << (sub_ctx_ ? sub_ctx_->errstr : "ctx 为空") << endl;
        close_conn();
        return false;
    }

    connected_ = true;
    return true;
}

void Redis_client::close_conn(){
    connected_ = false;

    //sub_ctx_ 只可能被接收线程或 stop() 使用，这里直接释放
    if(sub_ctx_ != nullptr){
        redisFree(sub_ctx_);
        sub_ctx_ = nullptr;
    }

    //pub_ctx_ 可能正被其他线程的 publish 使用，必须在锁内释放
    lock_guard<mutex> lk(pub_mtx_);
    if(pub_ctx_ != nullptr){
        redisFree(pub_ctx_);
        pub_ctx_ = nullptr;
    }
}

bool Redis_client::start(){
    if(running_){
        return true;
    }
    if(!open_conn()){
        return false;
    }
    running_ = true;
    recv_thread_ = thread(&Redis_client::recv_loop,this);
    return true;
}

void Redis_client::stop(){
    if(running_){
        running_ = false;
        //先 shutdown 打断阻塞的 select/recv，再 join，最后释放连接
        if(sub_ctx_ != nullptr){
            ::shutdown(sub_ctx_->fd,SHUT_RDWR);
        }
        if(recv_thread_.joinable()){
            recv_thread_.join();
        }
    }
    close_conn();
}

int Redis_client::publish(int channel,const string &msg){//往频道发一条消息
    lock_guard<mutex> lk(pub_mtx_);
    if(pub_ctx_ == nullptr || !connected_){
        return -1;
    }

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(pub_ctx_,"PUBLISH %d %b",channel,msg.c_str(),msg.size()));
    if(reply == nullptr){
        return -1;
    }

    int n = -1;
    if(reply->type == REDIS_REPLY_INTEGER){
        n = static_cast<int>(reply->integer);
    }
    freeReplyObject(reply);
    return n;
}

bool Redis_client::subscribe(int channel){//订阅某一个频道
    if(channel <= 0){
        return false;
    }
    lock_guard<mutex> lk(pend_mtx_);
    //台账里已经有这个频道，重复订阅没有意义
    if(!channels_.insert(channel).second){
        return true;
    }
    //只登记，真正的 SUBSCRIBE 由接收线程发出，避免跨线程操作 sub_ctx_
    pending_.push_back("SUBSCRIBE " + to_string(channel));
    return true;
}

bool Redis_client::unsubscribe(int channel){//取消订阅
    if(channel <= 0){
        return false;
    }
    lock_guard<mutex> lk(pend_mtx_);
    if(channels_.erase(channel) == 0){
        return true;
    }
    pending_.push_back("UNSUBSCRIBE " + to_string(channel));
    return true;
}

bool Redis_client::is_connected() const{
    return connected_;
}

void Redis_client::flush_pending(){ //这是将pending_队列里面积压的订阅/退订指令正真发出去的函数，由接收线程调用
    deque<string> cmds;
    {
        lock_guard<mutex> lk(pend_mtx_);
        cmds.swap(pending_);
    }

    for(const string &cmd : cmds){
        if(sub_ctx_ == nullptr || !send_only(sub_ctx_,cmd)){
            //发不出去就放回队首，等重连后再发
            lock_guard<mutex> lk(pend_mtx_);
            pending_.push_front(cmd);
            return;
        }
    }
}

bool Redis_client::send_only(redisContext *ctx,const string &cmd){//只负责发送命令，不读取回复
    if(ctx == nullptr){
        return false;
    }
    if(redisAppendCommand(ctx,cmd.c_str()) != REDIS_OK){
        return false;
    }
    int done = 0;
    while(!done){
        if(redisBufferWrite(ctx,&done) != REDIS_OK){
            return false;
        }
    }
    return true;
}

void Redis_client::reconnect(){//重连 
    close_conn();
    if(!open_conn()){
        return;
    }
    resubscribe_all();
}

void Redis_client::resubscribe_all(){//重连后补订所有频道
    lock_guard<mutex> lk(pend_mtx_);
    for(const int channel : channels_){
        pending_.push_back("SUBSCRIBE " + to_string(channel));
    }
}

void Redis_client::recv_loop(){//接收线程
    redisReply *reply = nullptr;

    while(running_){
        if(!connected_){
            this_thread::sleep_for(chrono::milliseconds(opt_.retry_ms));
            reconnect();
            continue;
        }

        flush_pending();
        if(sub_ctx_ == nullptr){
            connected_ = false;
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sub_ctx_->fd,&rfds);

        timeval tv{};
        tv.tv_sec = opt_.poll_ms / 1000;
        tv.tv_usec = (opt_.poll_ms % 1000) * 1000;

        const int n = select(sub_ctx_->fd + 1,&rfds,nullptr,nullptr,&tv);
        if(n == 0){
            continue;
        }
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            connected_ = false;
            continue;
        }

        if(redisGetReply(sub_ctx_,reinterpret_cast<void **>(&reply)) != REDIS_OK){
            connected_ = false;
            continue;
        }
        if(reply == nullptr){
            continue;
        }

        //只关心推送：["message", 频道, 正文]
        if(reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3 &&
           reply->element[0]->type == REDIS_REPLY_STRING){
            const string kind(reply->element[0]->str,reply->element[0]->len);
            if(kind == "message" && reply->element[1]->type == REDIS_REPLY_STRING &&
               reply->element[2]->type == REDIS_REPLY_STRING){
                int channel = 0;
                try{
                    channel = stoi(string(reply->element[1]->str,reply->element[1]->len));
                }catch(const exception &){
                    channel = 0;
                }
                const string payload(reply->element[2]->str,reply->element[2]->len);
                if(channel > 0 && handler_){
                    handler_(channel,payload);
                }
            }
        }

        freeReplyObject(reply);
        reply = nullptr;
    }
}
