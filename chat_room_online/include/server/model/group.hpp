// 群组模型：保存群自身的信息与群成员 id，并只操作自身状态；
// 什么时候调用这些操作由 server 的 service 层决定。
#ifndef SERVER_MODEL_GROUP_H
#define SERVER_MODEL_GROUP_H

#include <string>
#include <vector>

class Group {
private:
    int id_ = 0;                      // 群的编号
    int owner_id_ = 0;                // 群主的用户 id

    std::string name_;                // 群的名称
    std::string desc_;                // 群的描述

    // 群成员的用户 id 列表：只存 id，因此 Group 与 User 的头文件互不包含
    std::vector<int> member_ids_;

public:
    Group() = default;                // 特殊场景下需要默认构造
    Group(const std::string &name, const std::string &desc, int owner_id);

    int id() const;
    int owner_id() const;
    const std::string &name() const;
    const std::string &desc() const;
    const std::vector<int> &member_ids() const;
    int member_count() const;

    bool set_id(const int id);
    bool set_name(const std::string &name);
    bool set_desc(const std::string &desc);
    // 转让群主：实现时需要同时把新群主加入 member_ids_，保证群主一定是群成员
    bool set_owner_id(const int owner_id);

    bool add_member(const int user_id);
    bool del_member(const int user_id);
    bool has_member(const int user_id) const;
};



#endif
