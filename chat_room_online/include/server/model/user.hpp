// 用户模型：既保存用户自身的信息，也提供针对该用户的操作；
// 什么时候调用这些操作由 server 的 service 层决定。
#ifndef SERVER_MODEL_USER_H
#define SERVER_MODEL_USER_H

#include <string>
#include <vector>

class User {
private:
    int id_ = 0;                      // 用户编号（数据库自增主键）
    int conn_fd_ = -1;                // 通信描述符，-1 表示当前未连接
    std::string name_;                // 用户名
    std::string passwd_;              // 密码
    bool status_ = false;             // 在线状态

    std::vector<int> friend_ids_;     // 好友的用户 id 列表
    std::vector<int> group_ids_;      // 该用户已加入的群 id 列表

public:
    User(const std::string &name, const std::string &passwd);

    int id() const;
    int conn_fd() const;
    const std::string &name() const;
    const std::string &passwd() const;
    bool status() const;
    const std::vector<int> &friend_ids() const;
    const std::vector<int> &group_ids() const;

    bool set_id(const int id);
    bool set_conn_fd(const int conn_fd);
    bool set_status(const bool on);

    bool add_friend(const int friend_id);
    bool del_friend(const int friend_id);
    bool is_friend(const int friend_id) const;

    bool change_passwd(const std::string &new_passwd);

    bool add_group(const int group_id);
    bool exit_group(const int group_id);
};

#endif
