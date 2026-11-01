#include"chatservice.hpp"
#include"db.h"

#include<muduo/net/EventLoop.h>

#include<chrono>
#include<iostream>
#include<string>
#include<vector>

using namespace std;
using namespace muduo::net;

namespace{

Mysql_con_pool *pool(){
    return Mysql_con_pool::Ret_Entry();
}

long long now_sec(){
    return chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count();
}

//把用户输入转义后再拼进 SQL；拿不到连接时返回空串，让上层直接走 Db_Error
string esc(const string &s){
    MYSQL *con = pool()->get_connection();
    if(con == nullptr){
        return string();
    }
    string out(s.size() * 2 + 1,'\0');
    const unsigned long n = mysql_real_escape_string(con,&out[0],s.c_str(),
                                                      static_cast<unsigned long>(s.size()));
    out.resize(n);
    return out;
}

//查第一行第一列的整数，查不到或不是数字都返回 false
bool query_one(const string &sql,long long &out){
    MYSQL_RES *res = pool()->Que_to_sql(sql);
    if(res == nullptr){
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    bool ok = (row != nullptr && row[0] != nullptr);
    if(ok){
        try{
            out = stoll(row[0]);
        }catch(const exception &){
            ok = false;
        }
    }
    mysql_free_result(res);
    return ok;
}

//查第一列的所有整数
vector<int> query_ids(const string &sql){
    vector<int> ids;
    MYSQL_RES *res = pool()->Que_to_sql(sql);
    if(res == nullptr){
        return ids;
    }
    while(MYSQL_ROW row = mysql_fetch_row(res)){
        if(row[0] == nullptr){
            continue;
        }
        try{
            ids.push_back(stoi(row[0]));
        }catch(const exception &){
        }
    }
    mysql_free_result(res);
    return ids;
}

//协议解析统一入口，畸形 data 直接判参数错误
template <typename T>
bool parse_body(const Message &m,T &out){
    try{
        out = m.data.get<T>();
        return true;
    }catch(const exception &){
        return false;
    }
}

bool user_exist(int user_id){
    long long v = 0;
    return query_one("SELECT id FROM users WHERE id=" + to_string(user_id),v);
}

bool group_exist(int group_id){
    long long v = 0;
    return query_one("SELECT id FROM `groups` WHERE id=" + to_string(group_id),v);
}

bool group_has_member(int group_id,int user_id){
    long long v = 0;
    return query_one("SELECT id FROM group_members WHERE group_id=" + to_string(group_id) +
                     " AND user_id=" + to_string(user_id),v);
}

int group_owner(int group_id){
    MYSQL_RES *res = pool()->Que_to_sql("SELECT owner_id FROM `groups` WHERE id=" + to_string(group_id));
    if(res == nullptr){
        return 0;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    int owner = 0;
    if(row != nullptr && row[0] != nullptr){
        try{
            owner = stoi(row[0]);
        }catch(const exception &){
            owner = 0;
        }
    }
    mysql_free_result(res);
    return owner;
}

}

ChatService *ChatService::instance(){
    static ChatService service;
    return &service;
}

ChatService::ChatService(){
    init_handlers();

    //redis 只负责跨进程转发，连不上不影响单机使用
    Options opt;
    if(!load_options(opt)){
        cout << "读取 redis 配置失败，本次运行不启用跨进程推送" << endl;
        return;
    }
    redis_.set_options(opt);
    redis_.set_handler([this](int channel,const string &msg){ on_redis_message(channel,msg); });
    if(!redis_.start()){
        cout << "redis 未连接，本次运行不启用跨进程推送" << endl;
    }
}

void ChatService::stop(){
    redis_.stop();
}

void ChatService::init_handlers(){
    handlers_[Register_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_register(conn,m); };
    handlers_[Login_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_login(conn,m); };
    handlers_[Logout_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_logout(conn,m); };
    handlers_[Deregister_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_deregister(conn,m); };
    handlers_[Change_Passwd_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_change_passwd(conn,m); };
    handlers_[Add_Friend_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_add_friend(conn,m); };
    handlers_[Del_Friend_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_delete_friend(conn,m); };
    handlers_[Get_Friend_List_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_get_friend_list(conn,m); };
    handlers_[Create_Group_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_create_group(conn,m); };
    handlers_[Join_Group_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_join_group(conn,m); };
    handlers_[Exit_Group_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_exit_group(conn,m); };
    handlers_[Modify_Group_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_modify_group(conn,m); };
    handlers_[Transfer_Group_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_transfer_group(conn,m); };
    handlers_[Get_Group_List_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_get_group_list(conn,m); };
    handlers_[Get_Group_Members_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_get_group_member(conn,m); };
    handlers_[Chat_Private_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_chat_private(conn,m); };
    handlers_[Chat_Group_Req] = [this](const TcpConnectionPtr &conn,const Message &m){ handle_chat_group(conn,m); };
}

void ChatService::send_message(const TcpConnectionPtr &conn,const Message &m){
    if(!conn || !conn->connected()){
        return;
    }
    json j = m;
    string out = j.dump();
    out.push_back('\n');
    conn->send(out);
}

void ChatService::send_error(const TcpConnectionPtr &conn,const Message &req,int code){
    Message rsp;
    rsp.type = req.type + 1;
    rsp.seq = req.seq;
    rsp.code = code;
    send_message(conn,rsp);
}

int ChatService::user_id_of(const TcpConnectionPtr &conn) const{
    if(!conn){
        return 0;
    }
    const boost::any &ctx = conn->getContext();
    if(ctx.empty()){
        return 0;
    }
    try{
        return boost::any_cast<int>(ctx);
    }catch(const boost::bad_any_cast &){
        return 0;
    }
}

void ChatService::handle_message(const TcpConnectionPtr &conn,const string &line){
    Message m;
    try{
        m = json::parse(line).get<Message>();
    }catch(const exception &){
        Message rsp;
        rsp.code = Param_Error;
        send_message(conn,rsp);
        return;
    }

    const auto it = handlers_.find(m.type);
    if(it == handlers_.end()){
        send_error(conn,m,Param_Error);
        return;
    }

    //单个请求处理异常不能把 IO 线程带崩
    try{
        it->second(conn,m);
    }catch(const exception &e){
        cout << "处理消息异常: " << e.what() << endl;
        send_error(conn,m,Param_Error);
    }
}

void ChatService::client_close(const TcpConnectionPtr &conn){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        return;
    }

    {
        lock_guard<mutex> lk(mutex_);
        const auto it = online_.find(uid);
        //已经被新连接顶掉的旧连接，不能再清掉新连接的状态
        if(it == online_.end() || it->second != conn){
            return;
        }
        online_.erase(it);
    }

    conn->setContext(boost::any());
    redis_.unsubscribe(uid);
    pool()->Upd_to_sql("UPDATE users SET status=0 WHERE id=" + to_string(uid));
}

// ==================== 用户 ====================

void ChatService::handle_register(const TcpConnectionPtr &conn,const Message &m){
    RegisterReq req;
    if(!parse_body(m,req) || req.name.empty() || req.passwd.empty() ||
       req.name.size() > 64 || req.passwd.size() > 128){
        send_error(conn,m,Param_Error);
        return;
    }

    long long v = 0;
    if(query_one("SELECT id FROM users WHERE name='" + esc(req.name) + "'",v)){
        send_error(conn,m,User_Exist);
        return;
    }

    if(!pool()->Add_to_sql("INSERT INTO users(name,passwd,status,created_at) VALUES('" +
                           esc(req.name) + "','" + esc(req.passwd) + "',0," +
                           to_string(now_sec()) + ")")){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_login(const TcpConnectionPtr &conn,const Message &m){
    LoginReq req;
    if(!parse_body(m,req) || req.name.empty() || req.passwd.empty()){
        send_error(conn,m,Param_Error);
        return;
    }

    MYSQL_RES *res = pool()->Que_to_sql("SELECT id,passwd FROM users WHERE name='" + esc(req.name) + "'");
    if(res == nullptr){
        send_error(conn,m,Db_Error);
        return;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if(row == nullptr){
        mysql_free_result(res);
        send_error(conn,m,User_Not_Exist);
        return;
    }

    int uid = 0;
    string passwd;
    try{
        uid = stoi(row[0]);
        passwd = (row[1] != nullptr) ? row[1] : "";
    }catch(const exception &){
        mysql_free_result(res);
        send_error(conn,m,Db_Error);
        return;
    }
    mysql_free_result(res);

    if(passwd != req.passwd){
        send_error(conn,m,Passwd_Error);
        return;
    }

    //同一账号重复登录：旧连接让位给新连接
    TcpConnectionPtr old_conn;
    {
        lock_guard<mutex> lk(mutex_);
        const auto it = online_.find(uid);
        if(it != online_.end() && it->second != conn){
            old_conn = it->second;
        }
        online_[uid] = conn;
    }
    if(old_conn){
        old_conn->forceClose();
    }

    conn->setContext(uid);
    redis_.subscribe(uid);
    pool()->Upd_to_sql("UPDATE users SET status=1 WHERE id=" + to_string(uid));

    LoginRsp body;
    body.user_id = uid;
    body.name = req.name;
    body.friend_ids = query_ids("SELECT friend_id FROM friends WHERE user_id=" + to_string(uid));
    body.group_ids = query_ids("SELECT group_id FROM group_members WHERE user_id=" + to_string(uid));

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    rsp.data = body;
    send_message(conn,rsp);

    //回包之后再补投离线消息，避免客户端还在等登录响应时收到推送
    MYSQL_RES *off = pool()->Que_to_sql("SELECT from_id,group_id,content,created_at FROM offline_messages WHERE to_id=" +
                                        to_string(uid) + " ORDER BY id");
    if(off != nullptr){
        vector<ChatPush> pushes;
        while(MYSQL_ROW r = mysql_fetch_row(off)){
            try{
                ChatPush push;
                push.from_id = stoi(r[0]);
                push.group_id = stoi(r[1]);
                push.content = (r[2] != nullptr) ? r[2] : "";
                push.timestamp = stoll(r[3]);
                push.to_id = (push.group_id > 0) ? 0 : uid;
                pushes.push_back(push);
            }catch(const exception &){
            }
        }
        mysql_free_result(off);

        for(const ChatPush &push : pushes){
            Message pm;
            pm.type = Chat_Push;
            pm.data = push;
            send_message(conn,pm);
        }
        pool()->Del_to_sql("DELETE FROM offline_messages WHERE to_id=" + to_string(uid));
    }
}

void ChatService::handle_logout(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    LogoutReq req;
    if(!parse_body(m,req) || req.user_id != uid){
        send_error(conn,m,Param_Error);
        return;
    }

    {
        lock_guard<mutex> lk(mutex_);
        online_.erase(uid);
    }
    conn->setContext(boost::any());
    redis_.unsubscribe(uid);
    pool()->Upd_to_sql("UPDATE users SET status=0 WHERE id=" + to_string(uid));

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_deregister(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    DeregisterReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.passwd.empty()){
        send_error(conn,m,Param_Error);
        return;
    }

    long long v = 0;
    if(!query_one("SELECT passwd FROM users WHERE id=" + to_string(uid) +
                  " AND passwd='" + esc(req.passwd) + "'",v) &&
       query_one("SELECT id FROM users WHERE id=" + to_string(uid),v)){
        send_error(conn,m,Passwd_Error);
        return;
    }

    //先把自己拥有的群整体清掉，再清与自己相关的记录
    const vector<int> owned = query_ids("SELECT id FROM `groups` WHERE owner_id=" + to_string(uid));
    for(const int gid : owned){
        pool()->Del_to_sql("DELETE FROM group_members WHERE group_id=" + to_string(gid));
        pool()->Del_to_sql("DELETE FROM messages WHERE group_id=" + to_string(gid));
        pool()->Del_to_sql("DELETE FROM `groups` WHERE id=" + to_string(gid));
    }
    pool()->Del_to_sql("DELETE FROM friends WHERE user_id=" + to_string(uid) +
                       " OR friend_id=" + to_string(uid));
    pool()->Del_to_sql("DELETE FROM group_members WHERE user_id=" + to_string(uid));
    pool()->Del_to_sql("DELETE FROM messages WHERE from_id=" + to_string(uid) +
                       " OR to_id=" + to_string(uid));
    pool()->Del_to_sql("DELETE FROM offline_messages WHERE to_id=" + to_string(uid) +
                       " OR from_id=" + to_string(uid));

    if(!pool()->Del_to_sql("DELETE FROM users WHERE id=" + to_string(uid))){
        send_error(conn,m,Db_Error);
        return;
    }

    {
        lock_guard<mutex> lk(mutex_);
        online_.erase(uid);
    }
    conn->setContext(boost::any());
    redis_.unsubscribe(uid);

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_change_passwd(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    ChangePasswdReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.new_passwd.empty() ||
       req.new_passwd.size() > 128){
        send_error(conn,m,Param_Error);
        return;
    }

    if(!pool()->Upd_to_sql("UPDATE users SET passwd='" + esc(req.new_passwd) +
                          "' WHERE id=" + to_string(uid))){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

// ==================== 好友 ====================

void ChatService::handle_add_friend(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    AddFriendReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.friend_id <= 0 || req.friend_id == uid){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!user_exist(req.friend_id)){
        send_error(conn,m,User_Not_Exist);
        return;
    }

    long long v = 0;
    if(query_one("SELECT id FROM friends WHERE user_id=" + to_string(uid) +
                 " AND friend_id=" + to_string(req.friend_id),v)){
        send_error(conn,m,Friend_Exist);
        return;
    }

    const string ts = to_string(now_sec());
    if(!pool()->Add_to_sql("INSERT INTO friends(user_id,friend_id,created_at) VALUES(" +
                           to_string(uid) + "," + to_string(req.friend_id) + "," + ts + "),(" +
                           to_string(req.friend_id) + "," + to_string(uid) + "," + ts + ")")){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_delete_friend(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    DelFriendReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.friend_id <= 0){
        send_error(conn,m,Param_Error);
        return;
    }

    long long v = 0;
    if(!query_one("SELECT id FROM friends WHERE user_id=" + to_string(uid) +
                  " AND friend_id=" + to_string(req.friend_id),v)){
        send_error(conn,m,Friend_Not_Exist);
        return;
    }

    if(!pool()->Del_to_sql("DELETE FROM friends WHERE (user_id=" + to_string(uid) +
                           " AND friend_id=" + to_string(req.friend_id) + ") OR (user_id=" +
                           to_string(req.friend_id) + " AND friend_id=" + to_string(uid) + ")")){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_get_friend_list(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    GetFriendListReq req;
    if(!parse_body(m,req) || req.user_id != uid){
        send_error(conn,m,Param_Error);
        return;
    }

    GetFriendListRsp body;
    body.friend_ids = query_ids("SELECT friend_id FROM friends WHERE user_id=" + to_string(uid));

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    rsp.data = body;
    send_message(conn,rsp);
}

// ==================== 群组 ====================

void ChatService::handle_create_group(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    CreateGroupReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.name.empty() ||
       req.name.size() > 64 || req.desc.size() > 255){
        send_error(conn,m,Param_Error);
        return;
    }

    long long v = 0;
    if(query_one("SELECT id FROM `groups` WHERE name='" + esc(req.name) + "'",v)){
        send_error(conn,m,Group_Exist);
        return;
    }

    //成员去重、去自己，并且必须都是真实用户
    vector<int> members;
    for(const int id : req.member_ids){
        if(id <= 0 || id == uid){
            continue;
        }
        bool dup = false;
        for(const int x : members){
            if(x == id){
                dup = true;
                break;
            }
        }
        if(dup){
            continue;
        }
        if(!user_exist(id)){
            send_error(conn,m,User_Not_Exist);
            return;
        }
        members.push_back(id);
    }

    const string ts = to_string(now_sec());
    if(!pool()->Add_to_sql("INSERT INTO `groups`(name,`desc`,owner_id,created_at) VALUES('" +
                           esc(req.name) + "','" + esc(req.desc) + "'," + to_string(uid) +
                           "," + ts + ")")){
        send_error(conn,m,Db_Error);
        return;
    }

    if(!query_one("SELECT LAST_INSERT_ID()",v) || v <= 0){
        send_error(conn,m,Db_Error);
        return;
    }
    const int gid = static_cast<int>(v);

    //群主自己也要写进成员表
    string sql = "INSERT INTO group_members(group_id,user_id,joined_at) VALUES(" +
                 to_string(gid) + "," + to_string(uid) + "," + ts + ")";
    for(const int id : members){
        sql += ",(" + to_string(gid) + "," + to_string(id) + "," + ts + ")";
    }
    if(!pool()->Add_to_sql(sql)){
        send_error(conn,m,Db_Error);
        return;
    }

    CreateGroupRsp body;
    body.group_id = gid;

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    rsp.data = body;
    send_message(conn,rsp);
}

void ChatService::handle_join_group(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    JoinGroupReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.group_id <= 0){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!group_exist(req.group_id)){
        send_error(conn,m,Group_Not_Exist);
        return;
    }
    if(group_has_member(req.group_id,uid)){
        send_error(conn,m,Already_Group_Member);
        return;
    }

    if(!pool()->Add_to_sql("INSERT INTO group_members(group_id,user_id,joined_at) VALUES(" +
                           to_string(req.group_id) + "," + to_string(uid) + "," +
                           to_string(now_sec()) + ")")){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_exit_group(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    ExitGroupReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.group_id <= 0){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!group_exist(req.group_id)){
        send_error(conn,m,Group_Not_Exist);
        return;
    }
    if(!group_has_member(req.group_id,uid)){
        send_error(conn,m,Not_Group_Member);
        return;
    }
    //群主不能退群，只能先转让
    if(group_owner(req.group_id) == uid){
        send_error(conn,m,No_Group_Permission);
        return;
    }

    if(!pool()->Del_to_sql("DELETE FROM group_members WHERE group_id=" + to_string(req.group_id) +
                           " AND user_id=" + to_string(uid))){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_modify_group(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    ModifyGroupReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.group_id <= 0 ||
       req.name.size() > 64 || req.desc.size() > 255){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!group_exist(req.group_id)){
        send_error(conn,m,Group_Not_Exist);
        return;
    }
    if(group_owner(req.group_id) != uid){
        send_error(conn,m,No_Group_Permission);
        return;
    }

    string sql = "UPDATE `groups` SET ";
    bool first = true;
    //空串表示这一项不修改
    if(!req.name.empty()){
        long long v = 0;
        if(query_one("SELECT id FROM `groups` WHERE name='" + esc(req.name) +
                     "' AND id<>" + to_string(req.group_id),v)){
            send_error(conn,m,Group_Exist);
            return;
        }
        sql += "name='" + esc(req.name) + "'";
        first = false;
    }
    if(!req.desc.empty()){
        if(!first){
            sql += ",";
        }
        sql += "`desc`='" + esc(req.desc) + "'";
        first = false;
    }
    if(first){
        send_error(conn,m,Param_Error);
        return;
    }
    sql += " WHERE id=" + to_string(req.group_id);

    if(!pool()->Upd_to_sql(sql)){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_transfer_group(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    TransferGroupReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.group_id <= 0 || req.new_owner_id <= 0){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!group_exist(req.group_id)){
        send_error(conn,m,Group_Not_Exist);
        return;
    }
    if(group_owner(req.group_id) != uid){
        send_error(conn,m,No_Group_Permission);
        return;
    }
    if(req.new_owner_id == uid){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!user_exist(req.new_owner_id) || !group_has_member(req.group_id,req.new_owner_id)){
        send_error(conn,m,User_Not_Exist);
        return;
    }

    //原群主保留成员身份，只换 owner_id
    if(!pool()->Upd_to_sql("UPDATE `groups` SET owner_id=" + to_string(req.new_owner_id) +
                           " WHERE id=" + to_string(req.group_id))){
        send_error(conn,m,Db_Error);
        return;
    }

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    send_message(conn,rsp);
}

void ChatService::handle_get_group_list(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    GetGroupListReq req;
    if(!parse_body(m,req) || req.user_id != uid){
        send_error(conn,m,Param_Error);
        return;
    }

    GetGroupListRsp body;
    body.group_ids = query_ids("SELECT group_id FROM group_members WHERE user_id=" + to_string(uid));

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    rsp.data = body;
    send_message(conn,rsp);
}

void ChatService::handle_get_group_member(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    GetGroupMembersReq req;
    if(!parse_body(m,req) || req.user_id != uid || req.group_id <= 0){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!group_exist(req.group_id)){
        send_error(conn,m,Group_Not_Exist);
        return;
    }
    if(!group_has_member(req.group_id,uid)){
        send_error(conn,m,Not_Group_Member);
        return;
    }

    GetGroupMembersRsp body;
    body.group_id = req.group_id;
    body.owner_id = group_owner(req.group_id);
    body.member_ids = query_ids("SELECT user_id FROM group_members WHERE group_id=" +
                                to_string(req.group_id) + " ORDER BY id");

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    rsp.data = body;
    send_message(conn,rsp);
}

// ==================== 聊天 ====================

void ChatService::handle_chat_private(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    ChatPrivateReq req;
    if(!parse_body(m,req) || req.from_id != uid || req.to_id <= 0 || req.to_id == uid ||
       req.content.empty() || req.content.size() > 4096){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!user_exist(req.to_id)){
        send_error(conn,m,User_Not_Exist);
        return;
    }

    const long long ts = now_sec();
    if(!pool()->Add_to_sql("INSERT INTO messages(type,from_id,to_id,group_id,content,created_at) VALUES(" +
                           to_string(static_cast<int>(Chat_Private_Req)) + "," + to_string(uid) + "," +
                           to_string(req.to_id) + ",0,'" + esc(req.content) + "'," +
                           to_string(ts) + ")")){
        send_error(conn,m,Db_Error);
        return;
    }

    ChatPrivateRsp body;
    body.from_id = uid;
    body.to_id = req.to_id;
    body.timestamp = ts;

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    rsp.data = body;
    send_message(conn,rsp);

    ChatPush push;
    push.from_id = uid;
    push.to_id = req.to_id;
    push.content = req.content;
    push.timestamp = ts;
    send_or_offline(req.to_id,push);
}

void ChatService::handle_chat_group(const TcpConnectionPtr &conn,const Message &m){
    const int uid = user_id_of(conn);
    if(uid <= 0){
        send_error(conn,m,Not_Login);
        return;
    }

    ChatGroupReq req;
    if(!parse_body(m,req) || req.from_id != uid || req.group_id <= 0 ||
       req.content.empty() || req.content.size() > 4096){
        send_error(conn,m,Param_Error);
        return;
    }
    if(!group_exist(req.group_id)){
        send_error(conn,m,Group_Not_Exist);
        return;
    }
    if(!group_has_member(req.group_id,uid)){
        send_error(conn,m,Not_Group_Member);
        return;
    }

    const long long ts = now_sec();
    if(!pool()->Add_to_sql("INSERT INTO messages(type,from_id,to_id,group_id,content,created_at) VALUES(" +
                           to_string(static_cast<int>(Chat_Group_Req)) + "," + to_string(uid) +
                           ",0," + to_string(req.group_id) + ",'" + esc(req.content) + "'," +
                           to_string(ts) + ")")){
        send_error(conn,m,Db_Error);
        return;
    }

    ChatGroupRsp body;
    body.from_id = uid;
    body.group_id = req.group_id;
    body.timestamp = ts;

    Message rsp;
    rsp.type = m.type + 1;
    rsp.seq = m.seq;
    rsp.code = Ok;
    rsp.data = body;
    send_message(conn,rsp);

    ChatPush push;
    push.from_id = uid;
    push.group_id = req.group_id;
    push.content = req.content;
    push.timestamp = ts;

    //除自己以外，群成员逐个投递
    const vector<int> members = query_ids("SELECT user_id FROM group_members WHERE group_id=" +
                                          to_string(req.group_id));
    for(const int member : members){
        if(member != uid){
            send_or_offline(member,push);
        }
    }
}

// ==================== 推送 ====================

void ChatService::send_or_offline(int to_id,const ChatPush &push){
    TcpConnectionPtr target;
    {
        lock_guard<mutex> lk(mutex_);
        const auto it = online_.find(to_id);
        if(it != online_.end()){
            target = it->second;
        }
    }

    Message m;
    m.type = Chat_Push;
    m.code = Ok;
    m.data = push;

    if(target){
        //目标连接可能属于其它 IO 线程，必须切回它自己的循环再发送
        target->getLoop()->runInLoop([this,target,m](){ send_message(target,m); });
        return;
    }

    //本进程没有这个用户，才走 redis 投给其它进程
    json j = m;
    const int subscribers = redis_.publish(to_id,j.dump());
    if(subscribers <= 0){
        pool()->Add_to_sql("INSERT INTO offline_messages(to_id,from_id,group_id,content,created_at) VALUES(" +
                           to_string(to_id) + "," + to_string(push.from_id) + "," +
                           to_string(push.group_id) + ",'" + esc(push.content) + "'," +
                           to_string(push.timestamp) + ")");
    }
}

void ChatService::on_redis_message(int channel,const string &msg){
    Message m;
    try{
        m = json::parse(msg).get<Message>();
    }catch(const exception &){
        return;
    }

    TcpConnectionPtr target;
    {
        lock_guard<mutex> lk(mutex_);
        const auto it = online_.find(channel);
        if(it != online_.end()){
            target = it->second;
        }
    }
    if(!target){
        return;
    }

    //这里跑在 redis 接收线程上，只转到目标连接的 IO 线程，不碰数据库
    target->getLoop()->runInLoop([this,target,m](){ send_message(target,m); });
}
