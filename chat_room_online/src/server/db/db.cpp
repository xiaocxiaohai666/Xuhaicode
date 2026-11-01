#include"db.h"

#include<cstdlib>
#include<fstream>
#include<iostream>

using namespace std;

//没有经过 CMake 注入时（例如直接 g++ 编译单文件）退回当前目录
#ifndef SERVER_CONF_DIR
#define SERVER_CONF_DIR "."
#endif

//static 成员必须在 .cpp 里定义一次，否则链接时报 undefined reference
atomic<int> Mysql_con_pool::User_now{0};

Mysql_con_pool::Mysql_con_pool(){
    C_P_array = nullptr;
    status = false;
}

//单例入口：C++11 起局部静态变量的初始化是线程安全的，不需要自己加锁
Mysql_con_pool* Mysql_con_pool::Ret_Entry(){
    static Mysql_con_pool inst;
    return &inst;
}

//只分配槽位数组，不建立连接；真正的连库在 Con_sql 里做
bool Mysql_con_pool::Init_Con_vector(){
    if(Max_User <= 0){
        cout << "连接池容量必须大于 0，请先读取配置" << endl;
        return false;
    }
    //固定式连接池：一次性按容量分配好，之后只按下标访问，不增不删
    C_P_array = new Mysql_cli[Max_User];
    for(int i = 0;i < Max_User;++i){
        C_P_array[i].mysql_con = nullptr;
        C_P_array[i].status = false;//false 表示还没被线程领走
    }
    User_now = 0;
    return true;
}

//按容量把所有槽位的连接都建好
bool Mysql_con_pool::Con_sql(){
    if(C_P_array == nullptr){
        cout << "连接池数组尚未初始化" << endl;
        return false;
    }
    for(int i = 0;i < Max_User;++i){
        MYSQL* con = mysql_init(nullptr);//这是整个连接生命周期里唯一的操作入口，注意这是操作入口，不负责发起，以及断开连接的操作。
        //实际连接是由mysql_real_connect来创建发起连接
        if(con == nullptr){
            cout << "第 " << i << " 个连接 mysql_init 失败" << endl;
            return false;
        }
        //最后一个参数 0 表示用 TCP 连接；host 为 NULL 时才会走本地 socket
        if(mysql_real_connect(con,IP.c_str(),user.c_str(),Passwd.c_str(),
                              Database_name.c_str(),Port,nullptr,0) == nullptr){
            cout << "第 " << i << " 个连接失败: " << mysql_error(con) << endl;
            mysql_close(con);
            return false;
        }
        //字符集必须显式设置，否则中文昵称和聊天内容会乱码
        if(mysql_set_character_set(con,char_set.c_str()) != 0){
            cout << "第 " << i << " 个连接设置字符集失败: " << mysql_error(con) << endl;
            mysql_close(con);
            return false;
        }
        C_P_array[i].mysql_con = con;
    }
    return true;
}

Mysql_con_pool::~Mysql_con_pool(){
    if(C_P_array == nullptr){
        return;
    }
    for(int i = 0;i < Max_User;++i){
        if(C_P_array[i].mysql_con != nullptr){
            mysql_close(C_P_array[i].mysql_con);
            C_P_array[i].mysql_con = nullptr;
        }
    }
    delete[] C_P_array;
    C_P_array = nullptr;
}

//每个线程第一次调用时领一个下标，之后一直用自己那个槽位
//muduo 的 IO 线程池线程数固定，线程与下标天然一一对应，所以取连接是 O(1)，不需要轮询找空位
MYSQL* Mysql_con_pool::get_connection(){
    if(C_P_array == nullptr){
        cout << "连接池尚未初始化" << endl;
        return nullptr;
    }

    thread_local int slot = -1;
    if(slot < 0){
        const int idx = User_now.fetch_add(1);
        if(idx >= Max_User){
            cout << "线程数超过连接池容量 " << Max_User << "，请调大 conf 里的 pool_size" << endl;
            return nullptr;
        }
        //MySQL 客户端库要求每个用到它的线程先初始化自己的线程局部数据
        mysql_thread_init();
        slot = idx;
        C_P_array[slot].status = true;//true 表示这个槽位已经绑定给该线程
    }
    return C_P_array[slot].mysql_con;
}

bool Mysql_con_pool::Add_to_sql(const string msg){
    MYSQL* con = get_connection();
    if(con == nullptr){
        return false;
    }
    if(mysql_query(con,msg.c_str()) != 0){
        cout << "新增失败: " << mysql_error(con) << endl;
        return false;
    }
    return true;
}

bool Mysql_con_pool::Del_to_sql(const string msg){
    MYSQL* con = get_connection();
    if(con == nullptr){
        return false;
    }
    if(mysql_query(con,msg.c_str()) != 0){
        cout << "删除失败: " << mysql_error(con) << endl;
        return false;
    }
    return true;
}

bool Mysql_con_pool::Upd_to_sql(const string msg){
    MYSQL* con = get_connection();
    if(con == nullptr){
        return false;
    }
    if(mysql_query(con,msg.c_str()) != 0){
        cout << "更新失败: " << mysql_error(con) << endl;
        return false;
    }
    return true;
}

MYSQL_RES* Mysql_con_pool::Que_to_sql(const string msg){
    MYSQL* con = get_connection();
    if(con == nullptr){
        return nullptr;
    }
    if(mysql_query(con,msg.c_str()) != 0){
        cout << "查询失败: " << mysql_error(con) << endl;
        return nullptr;
    }
    //把结果集整块取回本地，连接马上就能干别的；结果集由调用方 mysql_free_result 释放
    MYSQL_RES* res = mysql_store_result(con);
    if(res == nullptr && mysql_errno(con) != 0){
        cout << "获取结果集失败: " << mysql_error(con) << endl;
    }
    return res;
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
        //兼容 Windows 换行：getline 只去掉 \n，\r 要单独处理，例如port=3306\n\r
        if(!line.empty() && line.back() == '\r'){
            line.pop_back();//尾删，删掉末尾的\r这种情况
        }

        //无内容直接跳过
        if(line.empty() || line[0] == '#'){
            continue;
        }
        //等价于size_t,存储等号的下标位置
        const string::size_type pos = line.find('=');
    
        //在string 里面npos相当于-1，也就是有内容，但是没有找到等号，那么直接显示配置错误。
        if(pos == string::npos){
            cout << file << " 第 " << num << " 行缺少等号" << endl;
            return false;
        }

        //切分函数，传两个参数就是从下标0开始，取pos个字符，不含下标Pos对应的字符。
        const string key = line.substr(0,pos);

        //从pos+1开始，一直取到末尾
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
    //缺了直接失败，避免拿空密码去连库
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
