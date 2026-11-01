#include"group.hpp"

#include<algorithm>

using namespace std;

Group::Group(const string &name,const string &desc,int owner_id)
    :owner_id_(owner_id),name_(name),desc_(desc){
    //群主必须同时是群成员，构造时就保证这个一致性
    if(owner_id_ > 0){
        member_ids_.push_back(owner_id_);
    }
}

int Group::id() const{
    return id_;
}

int Group::owner_id() const{
    return owner_id_;
}

const string &Group::name() const{
    return name_;
}

const string &Group::desc() const{
    return desc_;
}

const vector<int> &Group::member_ids() const{
    return member_ids_;
}

int Group::member_count() const{
    return static_cast<int>(member_ids_.size());
}

bool Group::set_id(const int id){
    if(id <= 0){
        return false;
    }
    id_ = id;
    return true;
}

bool Group::set_name(const string &name){
    if(name.empty()){
        return false;
    }
    name_ = name;
    return true;
}

bool Group::set_desc(const string &desc){
    desc_ = desc;
    return true;
}

bool Group::set_owner_id(const int owner_id){
    if(owner_id <= 0){
        return false;
    }
    owner_id_ = owner_id;
    //转让后新群主必须还是群成员，原群主保留成员身份
    if(!has_member(owner_id)){
        member_ids_.push_back(owner_id);
    }
    return true;
}

bool Group::add_member(const int user_id){
    if(user_id <= 0 || has_member(user_id)){
        return false;
    }
    member_ids_.push_back(user_id);
    return true;
}

bool Group::del_member(const int user_id){
    const auto it = find(member_ids_.begin(),member_ids_.end(),user_id);
    if(it == member_ids_.end()){
        return false;
    }
    member_ids_.erase(it);
    return true;
}

bool Group::has_member(const int user_id) const{
    return find(member_ids_.begin(),member_ids_.end(),user_id) != member_ids_.end();
}
