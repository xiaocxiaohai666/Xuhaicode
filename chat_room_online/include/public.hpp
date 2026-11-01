// 此文件是协议文件，也就是客户端与服务器端之间彼此操作的一些约定。
// 两端必须共用本文件，保证消息类型、返回码、字段名完全一致。
//这里将行为进行了规范，将行为的数据封装成了一个struct结构体，并且对不同消息（结构体）的收发接口进行了json化。
//

// 传输格式： JSON 正文（UTF-8）+自定义分隔符
// 正文示例：{"type":3,"seq":1,"code":0,"msg":"","data":{"name":"tom","passwd":"123"}}
//Req为request即请求，Rsp即Response。
#ifndef PUBLIC_HPP
#define PUBLIC_HPP

#include <string>
#include <vector>

#include "json.hpp"

// 给 nlohmann::json 起个别名，全文件统一用 json，省掉前缀
using json = nlohmann::json;

// 消息类型：请求以 _Req 结尾（request），响应以 _Rsp 结尾(response)，成对出现
enum MsgType : int {//指定类型为int
    // ---------- 用户 ----------
    Register_Req = 1,      // 注册
    Register_Rsp,
    Login_Req,             // 登录
    Login_Rsp,
    Logout_Req,            // 退出登录
    Logout_Rsp,
    Deregister_Req,        // 注销账号
    Deregister_Rsp,
    Change_Passwd_Req,     // 修改密码
    Change_Passwd_Rsp,

    // ---------- 好友 ----------
    Add_Friend_Req,        // 添加好友
    Add_Friend_Rsp,
    Del_Friend_Req,        // 删除好友
    Del_Friend_Rsp,
    Get_Friend_List_Req,   // 拉取好友列表
    Get_Friend_List_Rsp,

    // ---------- 群组 ----------
    Create_Group_Req,      // 建群
    Create_Group_Rsp,
    Join_Group_Req,        // 加入群
    Join_Group_Rsp,
    Exit_Group_Req,        // 退群
    Exit_Group_Rsp,
    Modify_Group_Req,      // 修改群名/群简介
    Modify_Group_Rsp,
    Transfer_Group_Req,    // 转让群主
    Transfer_Group_Rsp,
    Get_Group_List_Req,    // 拉取自己加入的群列表
    Get_Group_List_Rsp,
    Get_Group_Members_Req, // 拉取某个群的成员
    Get_Group_Members_Rsp,

    // ---------- 聊天 ----------
    Chat_Private_Req,      // 一对一发送
    Chat_Private_Rsp,
    Chat_Group_Req,        // 群内发送
    Chat_Group_Rsp,
    Chat_Push              // 服务端主动下发的消息（对方发来的 / 离线消息）
};

// 返回码，放在 Message::code 里
enum StatusCode : int {
    Ok = 0,                // 成功
    Param_Error,           // 参数缺失或格式错误
    User_Not_Exist,        // 用户不存在
    User_Exist,            // 用户名已被占用
    Passwd_Error,          // 密码错误
    Not_Login,             // 未登录
    Friend_Exist,          // 已经是好友
    Friend_Not_Exist,      // 不是好友
    Group_Not_Exist,       // 群不存在
    Group_Exist,           // 群名已存在
    Already_Group_Member,  // 已经在群里
    Not_Group_Member,      // 不在群里
    No_Group_Permission,   // 只有群主能做的操作
    Db_Error               // 数据库操作失败
};

// 统一消息信封：所有请求/响应都先包一层，业务字段放 data
//所有具体的业务消息都是通过外包一层message进行发送，具体的业务细分由data决定
struct Message {
    int type = 0;        // MsgType
    int seq = 0;         // 请求序号，服务端在响应里原样带回，便于客户端匹配
    int code = 0;        // StatusCode，请求时填 Ok，响应时填结果
    std::string msg;     // 附加说明，失败时放原因
    //协议层采用Niels Lohmann的C加加Json库
    json data = json::object();  // 业务字段（见下方各结构体）
    //object是一个静态工厂函数，返回一个空的Json对象{}。
    //有两个问题：为什么data不直接为空，因为我们设计的时候，就是默认已经有一个json载体，也就是说我们默认Message
    //本身就负责JSON对象的创建
    //为什么不用空值的Json形式呢，因为这个json的这个空值具有歧义，我这么写，这也是官方推荐使用的
};

inline void to_json(json &j, const Message &m) {
    j = json{{"type", m.type}, {"seq", m.seq}, {"code", m.code},
                       {"msg", m.msg}, {"data", m.data}};
}

inline void from_json(const json &j, Message &m) {
    j.at("type").get_to(m.type);//取出 "type" 这个键对应的 JSON 值（引用，不拷贝），把这个值转成 m.type 的类型（int）并写进 m.type
    j.at("seq").get_to(m.seq);
    j.at("code").get_to(m.code);
    // msg 和 data 允许缺省，避免客户端有空响应时直接解析失败
    if (j.find("msg") != j.end()) {
        j.at("msg").get_to(m.msg);
    }
    //j.find的作用就是查找是否有这个键，如果没有则是返回j.end
    if (j.find("data") != j.end()) {
        m.data = j.at("data");
    }
}

// ==================== 用户相关 ====================

struct RegisterReq {
    std::string name;    // 用户名
    std::string passwd;  // 密码
};

struct LoginReq {
    std::string name;    // 用户名
    std::string passwd;  // 密码
};

// 登录成功后返回该用户的完整快照，对应 User 模型里的字段
struct LoginRsp {
    int user_id = 0;                  // 用户 id（后续所有请求都要带）
    std::string name;                 // 用户名
    std::vector<int> friend_ids;      // friend_ids_
    std::vector<int> group_ids;       // group_ids_
};

struct LogoutReq {
    int user_id = 0;
};

struct DeregisterReq {
    int user_id = 0;
    std::string passwd;               // 注销需要再校验一次密码
};

struct ChangePasswdReq {
    int user_id = 0;
    std::string new_passwd;           // 新密码，对应 User::change_passwd
};

// ==================== 好友相关 ====================

struct AddFriendReq {
    int user_id = 0;                  // 发起者
    int friend_id = 0;                // 对方
};

struct DelFriendReq {
    int user_id = 0;                  // 发起者
    int friend_id = 0;                // 对方
};

struct GetFriendListReq {
    int user_id = 0;
};

struct GetFriendListRsp {
    std::vector<int> friend_ids;      // 对应 User::friend_ids()
};

// ==================== 群组相关 ====================

struct CreateGroupReq {
    int user_id = 0;                  // 创建者，自动成为群主
    std::string name;                 // name_
    std::string desc;                 // desc_
    std::vector<int> member_ids;      // 建群时一并拉进来的成员
};

struct CreateGroupRsp {
    int group_id = 0;                 // 数据库生成的群 id
};

struct JoinGroupReq {
    int user_id = 0;
    int group_id = 0;
};

struct ExitGroupReq {
    int user_id = 0;
    int group_id = 0;
};

// name / desc 传空串表示这一项不修改
struct ModifyGroupReq {
    int user_id = 0;                  // 操作者，必须是群主
    int group_id = 0;
    std::string name;                 // 对应 Group::set_name
    std::string desc;                 // 对应 Group::set_desc
};

struct TransferGroupReq {
    int user_id = 0;                  // 操作者，必须是原群主
    int group_id = 0;
    int new_owner_id = 0;             // 新群主，对应 Group::set_owner_id
};

struct GetGroupListReq {
    int user_id = 0;
};

struct GetGroupListRsp {
    std::vector<int> group_ids;       // 对应 User::group_ids()
};

struct GetGroupMembersReq {
    int user_id = 0;                  // 操作者，必须是群成员
    int group_id = 0;
};

struct GetGroupMembersRsp {
    int group_id = 0;
    int owner_id = 0;                 // 对应 Group::owner_id()
    std::vector<int> member_ids;      // 对应 Group::member_ids()
};

// ==================== 聊天相关 ====================

struct ChatPrivateReq {
    int from_id = 0;
    int to_id = 0;
    std::string content;              // 消息正文
    long long timestamp = 0;          // 客户端发送时间（秒）
};

struct ChatPrivateRsp {
    int from_id = 0;
    int to_id = 0;
    long long timestamp = 0;          // 服务端落库时间
};

struct ChatGroupReq {
    int from_id = 0;
    int group_id = 0;
    std::string content;
    long long timestamp = 0;
};

struct ChatGroupRsp {
    int from_id = 0;
    int group_id = 0;
    long long timestamp = 0;
};

// 服务端主动下发：群消息 group_id 有效、to_id 为 0；私聊消息 to_id 有效、group_id 为 0
struct ChatPush {
    int from_id = 0;
    int to_id = 0;
    int group_id = 0;
    std::string content;
    long long timestamp = 0;
};

// ==================== JSON 序列化 ====================
// 客户端发请求时：json j = req;  服务端解析时：auto req = msg.data.get<AddFriendReq>();

inline void to_json(json &j, const RegisterReq &v) {
    j = json{{"name", v.name}, {"passwd", v.passwd}};
}
inline void from_json(const json &j, RegisterReq &v) {
    j.at("name").get_to(v.name);
    j.at("passwd").get_to(v.passwd);
}

inline void to_json(json &j, const LoginReq &v) {
    j = json{{"name", v.name}, {"passwd", v.passwd}};
}
inline void from_json(const json &j, LoginReq &v) {
    j.at("name").get_to(v.name);
    j.at("passwd").get_to(v.passwd);
}

inline void to_json(json &j, const LoginRsp &v) {
    j = json{{"user_id", v.user_id}, {"name", v.name},
                       {"friend_ids", v.friend_ids}, {"group_ids", v.group_ids}};
}
inline void from_json(const json &j, LoginRsp &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("name").get_to(v.name);
    j.at("friend_ids").get_to(v.friend_ids);
    j.at("group_ids").get_to(v.group_ids);
}

inline void to_json(json &j, const LogoutReq &v) {
    j = json{{"user_id", v.user_id}};
}
inline void from_json(const json &j, LogoutReq &v) {
    j.at("user_id").get_to(v.user_id);
}

inline void to_json(json &j, const DeregisterReq &v) {
    j = json{{"user_id", v.user_id}, {"passwd", v.passwd}};
}
inline void from_json(const json &j, DeregisterReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("passwd").get_to(v.passwd);
}

inline void to_json(json &j, const ChangePasswdReq &v) {
    j = json{{"user_id", v.user_id}, {"new_passwd", v.new_passwd}};
}
inline void from_json(const json &j, ChangePasswdReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("new_passwd").get_to(v.new_passwd);
}

inline void to_json(json &j, const AddFriendReq &v) {
    j = json{{"user_id", v.user_id}, {"friend_id", v.friend_id}};
}
inline void from_json(const json &j, AddFriendReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("friend_id").get_to(v.friend_id);
}

inline void to_json(json &j, const DelFriendReq &v) {
    j = json{{"user_id", v.user_id}, {"friend_id", v.friend_id}};
}
inline void from_json(const json &j, DelFriendReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("friend_id").get_to(v.friend_id);
}

inline void to_json(json &j, const GetFriendListReq &v) {
    j = json{{"user_id", v.user_id}};
}
inline void from_json(const json &j, GetFriendListReq &v) {
    j.at("user_id").get_to(v.user_id);
}

inline void to_json(json &j, const GetFriendListRsp &v) {
    j = json{{"friend_ids", v.friend_ids}};
}
inline void from_json(const json &j, GetFriendListRsp &v) {
    j.at("friend_ids").get_to(v.friend_ids);
}

inline void to_json(json &j, const CreateGroupReq &v) {
    j = json{{"user_id", v.user_id}, {"name", v.name},
                       {"desc", v.desc}, {"member_ids", v.member_ids}};
}
inline void from_json(const json &j, CreateGroupReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("name").get_to(v.name);
    j.at("desc").get_to(v.desc);
    j.at("member_ids").get_to(v.member_ids);
}

inline void to_json(json &j, const CreateGroupRsp &v) {
    j = json{{"group_id", v.group_id}};
}
inline void from_json(const json &j, CreateGroupRsp &v) {
    j.at("group_id").get_to(v.group_id);
}

inline void to_json(json &j, const JoinGroupReq &v) {
    j = json{{"user_id", v.user_id}, {"group_id", v.group_id}};
}
inline void from_json(const json &j, JoinGroupReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("group_id").get_to(v.group_id);
}

inline void to_json(json &j, const ExitGroupReq &v) {
    j = json{{"user_id", v.user_id}, {"group_id", v.group_id}};
}
inline void from_json(const json &j, ExitGroupReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("group_id").get_to(v.group_id);
}

inline void to_json(json &j, const ModifyGroupReq &v) {
    j = json{{"user_id", v.user_id}, {"group_id", v.group_id},
                       {"name", v.name}, {"desc", v.desc}};
}
inline void from_json(const json &j, ModifyGroupReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("group_id").get_to(v.group_id);
    j.at("name").get_to(v.name);
    j.at("desc").get_to(v.desc);
}

inline void to_json(json &j, const TransferGroupReq &v) {
    j = json{{"user_id", v.user_id}, {"group_id", v.group_id},
                       {"new_owner_id", v.new_owner_id}};
}
inline void from_json(const json &j, TransferGroupReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("group_id").get_to(v.group_id);
    j.at("new_owner_id").get_to(v.new_owner_id);
}

inline void to_json(json &j, const GetGroupListReq &v) {
    j = json{{"user_id", v.user_id}};
}
inline void from_json(const json &j, GetGroupListReq &v) {
    j.at("user_id").get_to(v.user_id);
}

inline void to_json(json &j, const GetGroupListRsp &v) {
    j = json{{"group_ids", v.group_ids}};
}
inline void from_json(const json &j, GetGroupListRsp &v) {
    j.at("group_ids").get_to(v.group_ids);
}

inline void to_json(json &j, const GetGroupMembersReq &v) {
    j = json{{"user_id", v.user_id}, {"group_id", v.group_id}};
}
inline void from_json(const json &j, GetGroupMembersReq &v) {
    j.at("user_id").get_to(v.user_id);
    j.at("group_id").get_to(v.group_id);
}

inline void to_json(json &j, const GetGroupMembersRsp &v) {
    j = json{{"group_id", v.group_id}, {"owner_id", v.owner_id},
                       {"member_ids", v.member_ids}};
}
inline void from_json(const json &j, GetGroupMembersRsp &v) {
    j.at("group_id").get_to(v.group_id);
    j.at("owner_id").get_to(v.owner_id);
    j.at("member_ids").get_to(v.member_ids);
}

inline void to_json(json &j, const ChatPrivateReq &v) {
    j = json{{"from_id", v.from_id}, {"to_id", v.to_id},
                       {"content", v.content}, {"timestamp", v.timestamp}};
}
inline void from_json(const json &j, ChatPrivateReq &v) {
    j.at("from_id").get_to(v.from_id);
    j.at("to_id").get_to(v.to_id);
    j.at("content").get_to(v.content);
    j.at("timestamp").get_to(v.timestamp);
}

inline void to_json(json &j, const ChatPrivateRsp &v) {
    j = json{{"from_id", v.from_id}, {"to_id", v.to_id},
                       {"timestamp", v.timestamp}};
}
inline void from_json(const json &j, ChatPrivateRsp &v) {
    j.at("from_id").get_to(v.from_id);
    j.at("to_id").get_to(v.to_id);
    j.at("timestamp").get_to(v.timestamp);
}

inline void to_json(json &j, const ChatGroupReq &v) {
    j = json{{"from_id", v.from_id}, {"group_id", v.group_id},
                       {"content", v.content}, {"timestamp", v.timestamp}};
}
inline void from_json(const json &j, ChatGroupReq &v) {
    j.at("from_id").get_to(v.from_id);
    j.at("group_id").get_to(v.group_id);
    j.at("content").get_to(v.content);
    j.at("timestamp").get_to(v.timestamp);
}

inline void to_json(json &j, const ChatGroupRsp &v) {
    j = json{{"from_id", v.from_id}, {"group_id", v.group_id},
                       {"timestamp", v.timestamp}};
}
inline void from_json(const json &j, ChatGroupRsp &v) {
    j.at("from_id").get_to(v.from_id);
    j.at("group_id").get_to(v.group_id);
    j.at("timestamp").get_to(v.timestamp);
}

inline void to_json(json &j, const ChatPush &v) {
    j = json{{"from_id", v.from_id}, {"to_id", v.to_id},
                       {"group_id", v.group_id}, {"content", v.content},
                       {"timestamp", v.timestamp}};
}
inline void from_json(const json &j, ChatPush &v) {
    j.at("from_id").get_to(v.from_id);
    j.at("to_id").get_to(v.to_id);
    j.at("group_id").get_to(v.group_id);
    j.at("content").get_to(v.content);
    j.at("timestamp").get_to(v.timestamp);
}

#endif
