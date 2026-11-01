#include"user.hpp"

#include<algorithm>

using namespace std;

User::User(const string &name,const string &passwd):name_(name),passwd_(passwd){}

int User::id() const{
    return id_;
}

int User::conn_fd() const{
    return conn_fd_;
}

const string &User::name() const{
    return name_;
}

const string &User::passwd() const{
    return passwd_;
}

bool User::status() const{
    return status_;
}

const vector<int> &User::friend_ids() const{
    return friend_ids_;
}

const vector<int> &User::group_ids() const{
    return group_ids_;
}

bool User::set_id(const int id){
    if(id <= 0){
        return false;
    }
    id_ = id;
    return true;
}

bool User::set_conn_fd(const int conn_fd){
    conn_fd_ = conn_fd;
    return true;
}

bool User::set_status(const bool on){
    status_ = on;
    return true;
}

bool User::add_friend(const int friend_id){
    //不能加自己，也不能重复加
    if(friend_id <= 0 || friend_id == id_ || is_friend(friend_id)){
        return false;
    }
    friend_ids_.push_back(friend_id);
    return true;
}

bool User::del_friend(const int friend_id){
    const auto it = find(friend_ids_.begin(),friend_ids_.end(),friend_id);
    if(it == friend_ids_.end()){
        return false;
    }
    friend_ids_.erase(it);
    return true;
}

bool User::is_friend(const int friend_id) const{
    return find(friend_ids_.begin(),friend_ids_.end(),friend_id) != friend_ids_.end();
}

bool User::change_passwd(const string &new_passwd){
    if(new_passwd.empty()){
        return false;
    }
    passwd_ = new_passwd;
    return true;
}

bool User::add_group(const int group_id){
    if(group_id <= 0){
        return false;
    }
    //已经在群里就不要再加一条
    if(find(group_ids_.begin(),group_ids_.end(),group_id) != group_ids_.end()){
        return false;
    }
    group_ids_.push_back(group_id);
    return true;
}

bool User::exit_group(const int group_id){
    const auto it = find(group_ids_.begin(),group_ids_.end(),group_id);
    if(it == group_ids_.end()){
        return false;
    }
    group_ids_.erase(it);
    return true;
}
