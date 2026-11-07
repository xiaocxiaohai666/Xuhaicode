// 聊天室客户端：main 线程负责菜单与发送，独立接收线程负责收包和分发
//之前写的票务预约系统，基于票务预约系统的特殊性是可以将客户端进行串行化，即一个线程
//但是聊天室项目，是需要实现到微信那样，接收到消息的同时也能够去发送信息，这是至少需要两个线程，这里设计两个线程
// 协议见 include/public.hpp：JSON 正文 + '\n' 分隔符，外层统一 Message 信封
//整体设计是采用类似数据库中悲观锁的设计，尽量保证安全与效率的统一
#include"crypto.hpp"
#include"public.hpp"

#include<arpa/inet.h>
#include<netinet/in.h>
#include<sys/socket.h>
#include<unistd.h>

#include<atomic>
#include<cerrno>    //错误码
#include<chrono>    //时间戳的单位
#include<condition_variable>
#include<cstdint>   //设置一些数据的长度
#include<cstdlib>   //提供内存管理
#include<cstring>   //提供字符串的相关捏村操作
#include<ctime>
#include<fstream>   //文件流，读取客户端配置文件
#include<iostream>
#include<mutex>
#include<sstream>   //string stream即字符串流，可以将这个string 当成 cin/cout来进行使用
#include<string>
#include<thread>
#include<vector>

using namespace std;

namespace{

// ---------- 全局状态 ----------
//之所以用g开头的原因是因为这是全局变量，方便理解，所以grobal，用g开头
int g_fd = -1;                            // 与服务器通信的 socket
atomic<bool> g_running{true};        // 接收线程是否继续跑（能力）
atomic<bool> g_connected{true};      // 连接是否还在（状态）
atomic<int> g_seq{0};                // 请求序号，服务端原样带回

int g_user_id = 0;                        // 登录后才有值，0 表示未登录
string g_user_name;

// 一问一答的等待机制：同一时刻只允许一个未完成的请求
//这个请求的具体形式就是public.hpp协议里面的枚举值，客户端的其中一个请求没有解决，是不能够进行下一条请求的发送
mutex g_rsp_mtx;
condition_variable g_rsp_cv;
Message g_rsp;
bool g_has_rsp = false;//判断请求是否完成
int g_wait_seq = 0;//判断这是第几条请求

//连接配置

//没有经过 CMake 注入时（例如直接 g++ 编译单文件）退回当前目录
#ifndef CLIENT_CONF_DIR
#define CLIENT_CONF_DIR "."
#endif

//客户端只需要服务器地址和端口
struct Client_conf{
    string ip;
    int port = 0;
};

//按 key=value 的形式读取配置，规则与服务端 db.conf 一致
bool Read_Conf(const string &path,Client_conf &conf){
    const string file = path.empty() ? string(CLIENT_CONF_DIR "/client.conf") : path;

    ifstream in(file);
    if(!in.is_open()){
        cerr << "打开客户端配置失败: " << file << endl;
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
        //空行和 # 开头的注释行直接跳过
        if(line.empty() || line[0] == '#'){
            continue;
        }
        //只按第一个等号切分，值里允许再出现等号
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
            conf.port = atoi(value.c_str());
        }else{
            cerr << "未知配置项: " << key << "，位于 " << file << " 第 " << num << " 行" << endl;
        }
    }

    //必备项缺失就直接失败，避免拿空地址去连接
    if(conf.ip.empty()){
        cerr << file << " 缺少必填项 ip" << endl;
        return false;
    }
    if(conf.port <= 0 || conf.port > 65535){
        cerr << file << " 的 port 必须是 1~65535 的整数" << endl;
        return false;
    }
    return true;
}

// ---------- 小工具 ----------
string read_line(const string &tip){
    cout << tip;
    string s;
    getline(cin,s);
    return s;
}

int read_int(const string &tip,int def = 0){
    cout << tip;
    string s;
    getline(cin,s);
    if(s.empty()){
        return def;
    }
    //逐个字符校验：只有纯数字才转换，否则返回默认值
    //不能直接 atoi，因为它会把 "abc" 静默转成 0，菜单里就会被误判成"退出"
    for(const char c : s){
        if(c < '0' || c > '9'){
            return def;
        }
    }
    return atoi(s.c_str());
}

string format_time(long long ts){//ts就是时间戳，timestamp，时间戳是以秒为单位
    if(ts <= 0){
        return "-";
    }
    const time_t t = static_cast<time_t>(ts);//静态转换，将long long 类型强转为time_t类型，没有时区概念，是绝对时刻
    tm tmv{};
    localtime_r(&t,&tmv);//这个函数会将这个时间戳按照年月日来进行分解，并赋值到这个专门存储这个值的结构体
    //原来的t值并不会发生改变
    char buf[32]{0};//为什么要用缓冲区，因为我们要将这个时间戳拼进一条消息在整体输出。
    strftime(buf,sizeof(buf),"%H:%M:%S",&tmv);//按格式化输出，%H就是24小时制
    return buf;
}

const char* code_text(int code){
    switch(code){
        case Ok:                  return "成功";
        case Param_Error:         return "参数缺失或格式错误";
        case User_Not_Exist:      return "用户不存在";
        case User_Exist:          return "用户名已被占用";
        case Passwd_Error:        return "密码错误";
        case Not_Login:           return "未登录";
        case Friend_Exist:        return "已经是好友";
        case Friend_Not_Exist:    return "不是好友";
        case Group_Not_Exist:     return "群不存在";
        case Group_Exist:         return "群名已存在";
        case Already_Group_Member:return "已经在群里";
        case Not_Group_Member:    return "不在群里";
        case No_Group_Permission: return "没有群主权限";
        case Db_Error:            return "数据库操作失败";
        default:                  return "未知错误";
    }
}

void print_fail(const Message &rsp){
    cout << "失败(" << rsp.code << "): " << code_text(rsp.code);
    if(!rsp.msg.empty()){
        cout << " - " << rsp.msg;
    }
    cout << endl;
}

//send——all只负责发送的动作
//是属于request中功能的一部分。
bool send_all(const string &s){//将一整条消息完整的写进socket，因为缓冲区大小有限制，而要发送的信息可能会大于这个缓冲区，所以通过循环，保证包信息的连续性
    size_t sent = 0;
    while(sent < s.size()){
        // MSG_NOSIGNAL：对端关闭时不产生 SIGPIPE，改成 send 返回 -1
        const ssize_t n = send(g_fd,s.data() + sent,s.size() - sent,MSG_NOSIGNAL);
        //从已发送信息的下一个位置开始，发送剩余大小的数据。
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            return false;
        }
        if(n == 0){
            return false;
        }
        sent += static_cast<size_t>(n);//记录已经发送的包信息的位置
    }
    return true;
}


// 发送一条请求并等待对应序号的响应
bool request(int type,const json &data,Message &rsp){
    //先看看接收模块是否开启
    if(!g_connected){
        cout << "尚未连接服务器" << endl;
        return false;
    }

    Message req;
    req.type = type;
    req.seq = ++g_seq;//第一条消息就是1了，消息是递增的，表示这是第几条消息
    req.code = Ok;
    req.data = data;

    {
        lock_guard<mutex> lk(g_rsp_mtx);
        g_has_rsp = false;//这种资源同一时刻只能一个人线程改，所以必须上锁。
        g_wait_seq = req.seq;//同一个信息id
    }

    json j = req;//nlohman支持隐式转换，将Message的这个信息通过隐式转换成Json格式
    string out = j.dump();//这是nlohman的序列化函数，将一个json对象变成Json文本字符串

    //这上面的两个函数相当于实现了struct结构体转换成Json文本字符串（通过Json的专用函数）
    out = wire::encode(out);//明文模式补 '\n'，加密模式封成长度头+密文
    if(out.empty() || !send_all(out)){//send_all在这里！
        cout << "发送失败，连接可能已断开" << endl;
        return false;
    }

    unique_lock<mutex> lk(g_rsp_mtx);//RAII封装
    const bool ok = g_rsp_cv.wait_for(lk,chrono::seconds(5),[]{ return g_has_rsp || !g_connected; });
    if(!ok){
        cout << "等待服务器响应超时" << endl;
        return false;
    }
    if(!g_has_rsp){
        cout << "连接已断开，未收到响应" << endl;
        return false;
    }
    g_has_rsp = false;
    rsp = g_rsp;
    return true;
}

// ---------- 接收与分发 ----------
void print_push(const Message &m){
    ChatPush push;
    try{
        push = m.data.get<ChatPush>();//这是将data这个json对象转换成反序列化成ChatPush结构体。
        //`m.data.get<ChatPush>()` 能调用到你写的`from_json` ，靠的是 C++ 语言的 ADL（实参依赖查找）机制 ，并非函数包装器或虚函数。nlohmann 只是把这个机制封装成了一个约定，包装器名就叫`adl_serializer` ，名字本身就是提示。
        //如果出现异常会使线程崩溃，所以这里我们用try,catch
    }catch(const exception &){
        cout << "\n收到一条格式不正确的推送消息" << endl;
        return;
    }

    if(push.group_id > 0){
        cout << "\n[群 " << push.group_id << "] 用户" << push.from_id << " "
                  << format_time(push.timestamp) << ": " << push.content << endl;
    }else{
        cout << "\n[私聊] 用户" << push.from_id << " "
                  << format_time(push.timestamp) << ": " << push.content << endl;
    }
}

//接收方向，属于recv_loop中的一个功能模块，具体的接收模块
void dispatch_line(const string &line){
    Message m;
    try{
        json j = json::parse(line);//将文本形式的line转换成Json格式
        m = j.get<Message>();//将这个JSon格式转换成Message的报文形式，好进行读取操作
    }catch(const exception &e){
        cout << "\n收到无法解析的消息: " << e.what() << endl;
        return;
    }

    // 服务端主动下发的聊天消息：直接打印，不参与请求应答
    if(m.type == Chat_Push){
        print_push(m);
        return;
    }

    // 其余都是响应：序号对上才交给等待中的 request
    lock_guard<mutex> lk(g_rsp_mtx);
    if(m.seq == g_wait_seq){
        g_rsp = m;
        g_has_rsp = true;
        g_rsp_cv.notify_one();
    }
}

void recv_loop(){
    string buf;
    char tmp[4096];

    while(g_running){
        const ssize_t n = recv(g_fd,tmp,sizeof(tmp),0);
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            cout << "\n接收失败: " << strerror(errno) << endl;
            break;
        }
        if(n == 0){
            cout << "\n服务器已断开连接" << endl;
            break;
        }

        buf.append(tmp,static_cast<size_t>(n));

        size_t consumed = 0;//记录这一帧共占了多少字节
        string plain;//用来接收这一帧的正文
        while(wire::decode(buf.data(),buf.size(),consumed,plain)){
            buf.erase(0,consumed);
            if(!plain.empty()){
                dispatch_line(plain);
            }
        }

        //对端一直不发分隔符时，防止缓冲区无限增长
        //1u<<20 相当于1乘以2的20次方，结果是1048576，约等于1MB
        if(buf.size() > (1u << 20)){
            cout << "\n收到超长消息，丢弃缓冲区" << endl;
            buf.clear();
        }
    }

    g_connected = false;
    g_running = false;
    //唤醒可能还在等待响应的主线程
    {
        lock_guard<mutex> lk(g_rsp_mtx);
        g_rsp_cv.notify_all();
    }
    shutdown(g_fd,SHUT_RDWR);
}

// ---------- 业务操作 ----------
void do_register(){
    RegisterReq req;
    req.name = read_line("用户名: ");
    req.passwd = read_line("密码: ");

    Message rsp;
    json j = req;
    if(!request(Register_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "注册成功，请登录" << endl;
}

void do_login(){
    LoginReq req;
    req.name = read_line("用户名: ");
    req.passwd = read_line("密码: ");

    json j = req;
    Message rsp;
    if(!request(Login_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }

    LoginRsp body;
    try{
        body = rsp.data.get<LoginRsp>();
    }catch(const exception &){
        cout << "登录响应格式不正确" << endl;
        return;
    }

    g_user_id = body.user_id;
    g_user_name = body.name;

    cout << "登录成功，用户 id = " << g_user_id << "，昵称 " << g_user_name << endl;
    if(!body.friend_ids.empty()){
        cout << "好友 id:";
        for(const int id : body.friend_ids){
            cout << " " << id;
        }
        cout << endl;
    }
    if(!body.group_ids.empty()){
        cout << "群 id:";
        for(const int id : body.group_ids){
            cout << " " << id;
        }
        cout << endl;
    }
}

bool require_login(){
    if(g_user_id == 0){
        cout << "请先登录" << endl;
        return false;
    }
    return true;
}

void do_logout(){
    LogoutReq req;
    req.user_id = g_user_id;

    json j = req;
    Message rsp;
    if(!request(Logout_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "已退出登录" << endl;
    g_user_id = 0;
    g_user_name.clear();
}

void do_deregister(){
    DeregisterReq req;
    req.user_id = g_user_id;
    req.passwd = read_line("再次输入密码确认注销: ");

    json j = req;
    Message rsp;
    if(!request(Deregister_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "账号已注销" << endl;
    g_user_id = 0;
    g_user_name.clear();
}

void do_change_passwd(){
    ChangePasswdReq req;
    req.user_id = g_user_id;
    req.new_passwd = read_line("新密码: ");

    json j = req;
    Message rsp;
    if(!request(Change_Passwd_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "密码修改成功" << endl;
}

void do_get_friend_list(){
    GetFriendListReq req;
    req.user_id = g_user_id;

    json j = req;
    Message rsp;
    if(!request(Get_Friend_List_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }

    GetFriendListRsp body;
    try{
        body = rsp.data.get<GetFriendListRsp>();
    }catch(const exception &){
        cout << "好友列表响应格式不正确" << endl;
        return;
    }

    if(body.friend_ids.empty()){
        cout << "暂无好友" << endl;
        return;
    }
    cout << "好友 id:";
    for(const int id : body.friend_ids){
        cout << " " << id;
    }
    cout << endl;
}

void do_add_friend(){
    AddFriendReq req;
    req.user_id = g_user_id;
    req.friend_id = read_int("对方用户 id: ");

    json j = req;
    Message rsp;
    if(!request(Add_Friend_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "添加好友成功" << endl;
}

void do_del_friend(){
    DelFriendReq req;
    req.user_id = g_user_id;
    req.friend_id = read_int("要删除的好友 id: ");

    json j = req;
    Message rsp;
    if(!request(Del_Friend_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "已删除好友" << endl;
}

void do_create_group(){
    CreateGroupReq req;
    req.user_id = g_user_id;
    req.name = read_line("群名称: ");
    req.desc = read_line("群简介: ");

    const string ids = read_line("初始成员 id（空格分隔，可留空）: ");
    istringstream iss(ids);
    int id = 0;
    while(iss >> id){
        req.member_ids.push_back(id);
    }

    json j = req;
    Message rsp;
    if(!request(Create_Group_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }

    CreateGroupRsp body;
    try{
        body = rsp.data.get<CreateGroupRsp>();
    }catch(const exception &){
        cout << "建群响应格式不正确" << endl;
        return;
    }
    cout << "建群成功，群 id = " << body.group_id << endl;
}

void do_join_group(){
    JoinGroupReq req;
    req.user_id = g_user_id;
    req.group_id = read_int("群 id: ");

    json j = req;
    Message rsp;
    if(!request(Join_Group_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "已加入群" << endl;
}

void do_exit_group(){
    ExitGroupReq req;
    req.user_id = g_user_id;
    req.group_id = read_int("群 id: ");

    json j = req;
    Message rsp;
    if(!request(Exit_Group_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "已退出群" << endl;
}

void do_modify_group(){
    ModifyGroupReq req;
    req.user_id = g_user_id;
    req.group_id = read_int("群 id: ");
    req.name = read_line("新群名（留空表示不改）: ");
    req.desc = read_line("新简介（留空表示不改）: ");

    json j = req;
    Message rsp;
    if(!request(Modify_Group_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "群信息已更新" << endl;
}

void do_transfer_group(){
    TransferGroupReq req;
    req.user_id = g_user_id;
    req.group_id = read_int("群 id: ");
    req.new_owner_id = read_int("新群主 id: ");

    json j = req;
    Message rsp;
    if(!request(Transfer_Group_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "群主已转让" << endl;
}

void do_get_group_list(){
    GetGroupListReq req;
    req.user_id = g_user_id;

    json j = req;
    Message rsp;
    if(!request(Get_Group_List_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }

    GetGroupListRsp body;
    try{
        body = rsp.data.get<GetGroupListRsp>();
    }catch(const exception &){
        cout << "群列表响应格式不正确" << endl;
        return;
    }

    if(body.group_ids.empty()){
        cout << "暂未加入任何群" << endl;
        return;
    }
    cout << "群 id:";
    for(const int id : body.group_ids){
        cout << " " << id;
    }
    cout << endl;
}

void do_get_group_members(){
    GetGroupMembersReq req;
    req.user_id = g_user_id;
    req.group_id = read_int("群 id: ");

    json j = req;
    Message rsp;
    if(!request(Get_Group_Members_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }

    GetGroupMembersRsp body;
    try{
        body = rsp.data.get<GetGroupMembersRsp>();
    }catch(const exception &){
        cout << "群成员响应格式不正确" << endl;
        return;
    }

    cout << "群 " << body.group_id << "，群主 " << body.owner_id << endl;
    cout << "成员 id:";
    for(const int id : body.member_ids){
        cout << " " << id;
    }
    cout << endl;
}

void do_chat_private(){
    ChatPrivateReq req;
    req.from_id = g_user_id;
    req.to_id = read_int("对方用户 id: ");
    req.content = read_line("内容: ");
    req.timestamp = static_cast<long long>(time(nullptr));

    json j = req;
    Message rsp;
    if(!request(Chat_Private_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "已发送" << endl;
}

void do_chat_group(){
    ChatGroupReq req;
    req.from_id = g_user_id;
    req.group_id = read_int("群 id: ");
    req.content = read_line("内容: ");
    req.timestamp = static_cast<long long>(time(nullptr));

    json j = req;
    Message rsp;
    if(!request(Chat_Group_Req,j,rsp)){
        return;
    }
    if(rsp.code != Ok){
        print_fail(rsp);
        return;
    }
    cout << "已发送" << endl;
}

// ---------- 菜单 ----------
void print_menu(){
    cout << "\n========== 聊天室 ==========" << endl;
    if(g_user_id == 0){
        cout << " 1 注册\n 2 登录\n 0 退出程序" << endl;
    }else{
        cout << " 当前用户: " << g_user_name << " (id " << g_user_id << ")\n"
                  << " ---- 账号 ----\n"
                  << " 3 退出登录        4 注销账号        5 修改密码\n"
                  << " ---- 好友 ----\n"
                  << " 6 好友列表        7 添加好友        8 删除好友\n"
                  << " ---- 群组 ----\n"
                  << " 9 创建群         10 加入群         11 退出群\n"
                  << "12 修改群信息     13 转让群主       14 我的群列表\n"
                  << "15 群成员\n"
                  << " ---- 聊天 ----\n"
                  << "16 私聊           17 群聊\n"
                  << " 0 退出程序" << endl;
    }
    cout << "============================" << endl;
}

}

int main(int argc,char *argv[]){
    //argv[1] 可选，用于指定配置文件路径；不传就用 CMake 注入的默认目录
    if(argc > 2){
        cerr << "用法: " << argv[0] << " [配置文件路径]" << endl;
        return 1;
    }

    Client_conf conf;
    if(!Read_Conf(argc > 1 ? argv[1] : "",conf)){
        return 1;
    }

    const char *ip = conf.ip.c_str();
    const int port = conf.port;

    g_fd = socket(AF_INET,SOCK_STREAM,0);
    if(g_fd < 0){
        cerr << "创建 socket 失败: " << strerror(errno) << endl;
        return 1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<uint16_t>(port));
    if(inet_pton(AF_INET,ip,&server.sin_addr) != 1){
        cerr << "服务器地址不合法: " << ip << endl;
        close(g_fd);
        return 1;
    }

    if(connect(g_fd,reinterpret_cast<sockaddr *>(&server),sizeof(server)) < 0){
        cerr << "连接服务器失败: " << strerror(errno) << endl;
        close(g_fd);
        return 1;
    }

    cout << "已连接 " << ip << ":" << port << "，输入菜单编号进行操作" << endl;

    thread recv_thread(recv_loop);

    while(g_running){
        print_menu();
        const int choice = read_int("请选择: ",-1);

        switch(choice){
            case 0:
                g_running = false;
                break;
            case 1:
                do_register();
                break;
            case 2:
                do_login();
                break;
            case 3:
                if(require_login()) do_logout();
                break;
            case 4:
                if(require_login()) do_deregister();
                break;
            case 5:
                if(require_login()) do_change_passwd();
                break;
            case 6:
                if(require_login()) do_get_friend_list();
                break;
            case 7:
                if(require_login()) do_add_friend();
                break;
            case 8:
                if(require_login()) do_del_friend();
                break;
            case 9:
                if(require_login()) do_create_group();
                break;
            case 10:
                if(require_login()) do_join_group();
                break;
            case 11:
                if(require_login()) do_exit_group();
                break;
            case 12:
                if(require_login()) do_modify_group();
                break;
            case 13:
                if(require_login()) do_transfer_group();
                break;
            case 14:
                if(require_login()) do_get_group_list();
                break;
            case 15:
                if(require_login()) do_get_group_members();
                break;
            case 16:
                if(require_login()) do_chat_private();
                break;
            case 17:
                if(require_login()) do_chat_group();
                break;
            default:
                cout << "无效的选项" << endl;
                break;
        }
    }

    g_running = false;
    g_connected = false;
    shutdown(g_fd,SHUT_RDWR);
    if(recv_thread.joinable()){
        recv_thread.join();
    }
    close(g_fd);
    g_fd = -1;

    cout << "已退出" << endl;
    return 0;
}
