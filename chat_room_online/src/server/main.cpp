// 聊天室服务端入口：先把数据库和业务层准备好，再启动 muduo 事件循环
#include"crypto.hpp"
#include"chatserver.hpp"
#include"chatservice.hpp"
#include"db.h"

#include<muduo/net/EventLoop.h>

#include<csignal>
#include<fstream>
#include<iostream>
#include<string>

using namespace std;
using namespace muduo::net;

//没有经过 CMake 注入时（例如直接 g++ 编译单文件）退回当前目录
#ifndef SERVER_CONF_DIR
#define SERVER_CONF_DIR "."
#endif

namespace{

//服务端监听参数
struct Server_conf{
    string ip = "0.0.0.0";
    int port = 6000;
    int threads = 4;
};

EventLoop *g_loop = nullptr;

//信号处理里只做一件事：让事件循环退出，真正的清理放在主流程里
void on_signal(int){
    if(g_loop != nullptr){
        g_loop->quit();
    }
}

//配置里的数字先校验成纯数字再转换，避免 atoi 把非法输入静默转成 0
bool parse_int(const string &value,int &out){
    if(value.empty()){
        return false;
    }
    for(const char c : value){
        if(c < '0' || c > '9'){
            return false;
        }
    }
    out = stoi(value);
    return true;
}

bool read_conf(const string &path,Server_conf &conf){
    const string file = path.empty() ? string(SERVER_CONF_DIR "/server.conf") : path;

    ifstream in(file);
    if(!in.is_open()){
        cerr << "打开服务端配置失败: " << file << endl;
        return false;
    }

    string line;
    int num = 0;
    while(getline(in,line)){
        ++num;
        //兼容 Windows 换行
        if(!line.empty() && line.back() == '\r'){
            line.pop_back();
        }
        if(line.empty() || line[0] == '#'){
            continue;
        }
        const string::size_type pos = line.find('=');
        if(pos == string::npos){
            cerr << file << " 第 " << num << " 行缺少等号" << endl;
            return false;
        }
        const string key = line.substr(0,pos);
        const string value = line.substr(pos + 1);

        if(key == "ip"){
            conf.ip = value;
        }else if(key == "port"){
            if(!parse_int(value,conf.port)){
                cerr << file << " 第 " << num << " 行的 port 不是整数" << endl;
                return false;
            }
        }else if(key == "threads"){
            if(!parse_int(value,conf.threads)){
                cerr << file << " 第 " << num << " 行的 threads 不是整数" << endl;
                return false;
            }
        }else{
            cerr << "未知配置项: " << key << "，位于 " << file << " 第 " << num << " 行" << endl;
        }
    }

    if(conf.ip.empty()){
        cerr << file << " 缺少必填项 ip" << endl;
        return false;
    }
    if(conf.port <= 0 || conf.port > 65535){
        cerr << file << " 的 port 必须是 1~65535" << endl;
        return false;
    }
    if(conf.threads <= 0){
        cerr << file << " 的 threads 必须是正整数" << endl;
        return false;
    }
    return true;
}

}

int main(int argc,char *argv[]){
    //argv[1] 可选，用于指定配置文件路径
    if(argc > 2){
        cerr << "用法: " << argv[0] << " [配置文件路径]" << endl;
        return 1;
    }

    Server_conf conf;
    if(!read_conf(argc > 1 ? argv[1] : "",conf)){
        return 1;
    }

    //必须先建好连接池，业务层后续才能直接取连接
    //加密传输：编译期开关决定是否真的加解密；明文模式下 init 是空操作
    if(!wire::init()){
        cerr << "加密模块初始化失败" << endl;
        return 1;
    }

    Mysql_con_pool *pool = Mysql_con_pool::Ret_Entry();
    if(!pool->Read_Conf("") || !pool->Init_Con_vector() || !pool->Con_sql()){
        cerr << "数据库初始化失败，请检查 db.conf 与 MySQL 服务" << endl;
        return 1;
    }

    //单例构造里会加载 redis 配置并启动接收线程；redis 不可用时只告警，不影响单体运行
    ChatService::instance();

    EventLoop loop;
    g_loop = &loop;
    signal(SIGINT,on_signal);
    signal(SIGTERM,on_signal);

    Chat_server server(&loop,conf.ip,conf.port,"ChatServer",conf.threads);
    server.start();
    cout << "服务端已启动，监听 " << conf.ip << ":" << conf.port
         << "，IO 线程数 " << conf.threads << endl;

    loop.loop();

    //退出顺序：先停 redis 接收线程，再按作用域销毁网络层，最后连接池静态析构关库
    ChatService::instance()->stop();
    cout << "服务端已退出" << endl;
    return 0;
}
