#ifndef DB_H
#define DB_H

#include<mysql/mysql.h>

#include<chrono>
#include<condition_variable>
#include<mutex>
#include<queue>
#include<string>
#include<vector>

enum OPERA_MYSQL{
   add = 1,
   del,
   update,
   query
};

//通用连接池：空闲下标队列 + 条件变量 + RAII 归还 + 断线重建
//与旧版的区别：连接是共享资源，谁用谁借、用完归还，不再和线程终身绑定
class Mysql_con_pool{
public:
    //RAII 句柄：离开作用域自动归还，调用方不需要记着还
    class Guard{
    public:
        Guard() = default;
        Guard(Mysql_con_pool *pool,int idx,MYSQL *con)
            :pool_(pool),idx_(idx),con_(con){}
        ~Guard();
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
        Guard(Guard &&o) noexcept;
        Guard &operator=(Guard &&o) noexcept;

        bool valid() const { return con_ != nullptr; }
        MYSQL *get() const { return con_; }
        bool reconnect();
        void release();

    private:
        Mysql_con_pool *pool_ = nullptr;
        int idx_ = -1;
        MYSQL *con_ = nullptr;
    };

    static Mysql_con_pool *Ret_Entry();
    Mysql_con_pool(const Mysql_con_pool &) = delete;
    Mysql_con_pool &operator=(const Mysql_con_pool &) = delete;

    bool Read_Conf(const std::string &path = "");
    bool Init_Con_vector();
    bool Con_sql();
    ~Mysql_con_pool();

    //借一条连接，池空时最多等 timeout_ms；拿不到返回无效 Guard
    Guard acquire(int timeout_ms = 3000);

    bool Add_to_sql(const std::string &msg);
    bool Del_to_sql(const std::string &msg);
    bool Upd_to_sql(const std::string &msg);
    MYSQL_RES *Que_to_sql(const std::string &msg);

    //转义要用到连接的字符集信息，统一由池借一条连接来做，做完自动归还
    std::string escape(const std::string &s);

private:
    Mysql_con_pool();
    void release_slot(int idx);
    MYSQL *create_one();
    static bool conn_lost(unsigned int err);
    bool exec_write(const char *what,const std::string &msg);

    std::string IP;
    int Port = 0;
    std::string user;
    std::string Passwd;
    std::string Database_name;
    std::string char_set;
    int Max_User = 0;

    std::vector<MYSQL*> conns_;
    std::queue<int> idle_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif
