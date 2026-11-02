#include"db.h"

#include<mysql/errmsg.h>

#include<cstdlib>
#include<fstream>
#include<iostream>

using namespace std;

//没有经过 CMake 注入时（例如直接 g++ 编译单文件）退回当前目录
#ifndef SERVER_CONF_DIR
#define SERVER_CONF_DIR "."
#endif

namespace{
//MySQL 客户端库要求每个用到它的线程先初始化自己的线程局部数据，每线程只做一次
thread_local bool tls_mysql_inited = false;

void ensure_thread_inited(){
    if(!tls_mysql_inited){
        mysql_thread_init();
        tls_mysql_inited = true;
    }
}
}

Mysql_con_pool::Mysql_con_pool() = default;

//单例入口：C++11 起局部静态变量的初始化是线程安全的，不需要自己加锁
Mysql_con_pool *Mysql_con_pool::Ret_Entry(){
    static Mysql_con_pool inst;
    return &inst;
}

// ---------------- Guard：借还的载体 ----------------

Mysql_con_pool::Guard::~Guard(){
    release();
}

Mysql_con_pool::Guard::Guard(Guard &&o) noexcept
    :pool_(o.pool_),idx_(o.idx_),con_(o.con_){
    o.pool_ = nullptr;
    o.idx_ = -1;
    o.con_ = nullptr;
}

Mysql_con_pool::Guard &Mysql_con_pool::Guard::operator=(Guard &&o) noexcept{
    if(this != &o){
        release();
        pool_ = o.pool_;
        idx_ = o.idx_;
        con_ = o.con_;
        o.pool_ = nullptr;
        o.idx_ = -1;
        o.con_ = nullptr;
    }
    return *this;
}

void Mysql_con_pool::Guard::release(){
    if(pool_ != nullptr && idx_ >= 0){
        pool_->release_slot(idx_);
    }
    pool_ = nullptr;
    idx_ = -1;
    con_ = nullptr;
}

//断线重建：关掉旧的，按同一份配置重新连一条，写回池子数组
//idx_ 由本句柄独占（已经从空闲队列里取出来了），其他线程拿不到，所以直接改不用加锁
bool Mysql_con_pool::Guard::reconnect(){
    if(pool_ == nullptr || idx_ < 0){
        return false;
    }
    if(con_ != nullptr){
        mysql_close(con_);
        con_ = nullptr;
    }
    MYSQL *fresh = pool_->create_one();
    if(fresh == nullptr){
        pool_->conns_[idx_] = nullptr;
        return false;
    }
    con_ = fresh;
    pool_->conns_[idx_] = fresh;
    return true;
}

// ---------------- 池本体 ----------------

bool Mysql_con_pool::Init_Con_vector(){
    if(Max_User <= 0){
        cout << "连接池容量必须大于 0，请先读取配置" << endl;
        return false;
    }
    lock_guard<mutex> lk(mtx_);
    conns_.assign(static_cast<size_t>(Max_User),nullptr);
    queue<int> empty;
    idle_.swap(empty);
    return true;
}

MYSQL *Mysql_con_pool::create_one(){
    MYSQL *con = mysql_init(nullptr);
    if(con == nullptr){
        cout << "mysql_init 失败" << endl;
        return nullptr;
    }
    //MySQL 不可达时不要长时间卡住调用方
    unsigned int connect_timeout = 3;
    mysql_options(con,MYSQL_OPT_CONNECT_TIMEOUT,&connect_timeout);

    if(mysql_real_connect(con,IP.c_str(),user.c_str(),Passwd.c_str(),
                          Database_name.c_str(),Port,nullptr,0) == nullptr){
        cout << "建连失败: " << mysql_error(con) << endl;
        mysql_close(con);
        return nullptr;
    }
    if(mysql_set_character_set(con,char_set.c_str()) != 0){
        cout << "设置字符集失败: " << mysql_error(con) << endl;
        mysql_close(con);
        return nullptr;
    }
    return con;
}

bool Mysql_con_pool::Con_sql(){
    if(conns_.size() != static_cast<size_t>(Max_User)){
        cout << "连接池数组尚未初始化" << endl;
        return false;
    }
    for(int i = 0;i < Max_User;++i){
        MYSQL *c = create_one();
        if(c == nullptr){
            //一条都建不起来就整体失败，并把已经建好的关掉，不留半成品
            for(int j = 0;j < i;++j){
                if(conns_[j] != nullptr){
                    mysql_close(conns_[j]);
                    conns_[j] = nullptr;
                }
            }
            return false;
        }
        conns_[i] = c;
    }

    lock_guard<mutex> lk(mtx_);
    for(int i = 0;i < Max_User;++i){
        idle_.push(i);
    }
    return true;
}

Mysql_con_pool::~Mysql_con_pool(){
    for(MYSQL *c : conns_){
        if(c != nullptr){
            mysql_close(c);
        }
    }
    conns_.clear();
}

Mysql_con_pool::Guard Mysql_con_pool::acquire(int timeout_ms){
    ensure_thread_inited();

    unique_lock<mutex> lk(mtx_);
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(timeout_ms);

    for(;;){
        //池空就等，等到有人归还或者超时
        if(!cv_.wait_until(lk,deadline,[this]{ return !idle_.empty(); })){
            return Guard();
        }
        const int idx = idle_.front();
        idle_.pop();

        if(conns_[idx] != nullptr){
            return Guard(this,idx,conns_[idx]);
        }

        //这个槽位上的连接之前坏过被清空了，锁外重建，别在持锁时做网络 IO
        lk.unlock();
        MYSQL *fresh = create_one();
        lk.lock();
        if(fresh != nullptr){
            conns_[idx] = fresh;
            return Guard(this,idx,fresh);
        }
        idle_.push(idx);//重建也失败，槽位放回去，本次直接失败
        return Guard();
    }
}

void Mysql_con_pool::release_slot(int idx){
    {
        lock_guard<mutex> lk(mtx_);
        idle_.push(idx);
    }
    cv_.notify_one();
}

//判断错误码是不是"连接没了"，只有这种才值得重连
bool Mysql_con_pool::conn_lost(unsigned int err){
    return err == CR_SERVER_GONE_ERROR
        || err == CR_SERVER_LOST
        || err == CR_CONN_HOST_ERROR
        || err == CR_CONNECTION_ERROR;
}

bool Mysql_con_pool::exec_write(const char *what,const string &msg){
    Guard g = acquire();
    if(!g.valid()){
        cout << what << "失败: 等待连接超时" << endl;
        return false;
    }
    if(mysql_query(g.get(),msg.c_str()) == 0){
        return true;
    }
    const unsigned int e = mysql_errno(g.get());
    cout << what << "失败: " << mysql_error(g.get()) << endl;
    //写操作不重试：连接断掉时语句可能已经执行过，重试会重复插入
    //只把这条死连接换掉，让下一次请求用新的
    if(conn_lost(e)){
        g.reconnect();
    }
    return false;
}

bool Mysql_con_pool::Add_to_sql(const string &msg){
    return exec_write("新增",msg);
}

bool Mysql_con_pool::Del_to_sql(const string &msg){
    return exec_write("删除",msg);
}

bool Mysql_con_pool::Upd_to_sql(const string &msg){
    return exec_write("更新",msg);
}

MYSQL_RES *Mysql_con_pool::Que_to_sql(const string &msg){
    Guard g = acquire();
    if(!g.valid()){
        cout << "查询失败: 等待连接超时" << endl;
        return nullptr;
    }
    int retried = 0;
    for(;;){
        if(mysql_query(g.get(),msg.c_str()) == 0){
            //把结果集整块取回本地，连接马上就能还回去；结果集由调用方 mysql_free_result 释放
            MYSQL_RES *res = mysql_store_result(g.get());
            if(res == nullptr && mysql_errno(g.get()) != 0){
                cout << "获取结果集失败: " << mysql_error(g.get()) << endl;
            }
            return res;
        }
        const unsigned int e = mysql_errno(g.get());
        cout << "查询失败: " << mysql_error(g.get()) << endl;
        //读是幂等的，断线可以重连后重试一次
        if(conn_lost(e) && retried == 0 && g.reconnect()){
            ++retried;
            continue;
        }
        return nullptr;
    }
}

string Mysql_con_pool::escape(const string &s){
    Guard g = acquire();
    if(!g.valid()){
        return string();
    }
    string out(s.size() * 2 + 1,'\0');
    const unsigned long n = mysql_real_escape_string(
        g.get(),&out[0],s.c_str(),static_cast<unsigned long>(s.size()));
    out.resize(n);
    return out;
}

bool Mysql_con_pool::Read_Conf(const string &path){
    const string file = path.empty() ? string(SERVER_CONF_DIR "/db.conf") : path;

    ifstream in(file);//定义并打开一个文件流对象，按照所传路径打开文件
    if(!in.is_open()){
        cout << "打开数据库配置失败: " << file << endl;
        return false;
    }

    string line;
    int num = 0;
    while(getline(in,line)){
        ++num;
        //兼容 Windows 换行：getline 只去掉 \n，\r 要单独处理
        if(!line.empty() && line.back() == '\r'){
            line.pop_back();
        }
        if(line.empty() || line[0] == '#'){
            continue;
        }
        const string::size_type pos = line.find('=');
        if(pos == string::npos){
            cout << file << " 第 " << num << " 行缺少等号" << endl;
            return false;
        }
        const string key = line.substr(0,pos);
        const string value = line.substr(pos + 1);

        if(key == "ip"){
            IP = value;
        }else if(key == "port"){
            Port = atoi(value.c_str());
        }else if(key == "name"){
            Database_name = value;
        }else if(key == "user"){
            user = value;
        }else if(key == "passwd"){
            Passwd = value;
        }else if(key == "charset"){
            char_set = value;
        }else if(key == "pool_size"){
            Max_User = atoi(value.c_str());
        }else{
            cout << "未知配置项: " << key << "，位于 " << file << " 第 " << num << " 行" << endl;
        }
    }

    //保险，必须所有数据都拿到，避免缺失关键数据
    if(IP.empty() || Database_name.empty() || user.empty() || Passwd.empty()){
        cout << file << " 缺少必填项 ip/name/user/passwd" << endl;
        return false;
    }
    if(Port <= 0 || Max_User <= 0){
        cout << file << " 的 port / pool_size 必须为正整数" << endl;
        return false;
    }
    return true;
}
