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
    class Guard{//这个类的作用相当于图书馆的图书管理员一样， 它会将谁借了这本书自动记在小本子上，如果归还回来了，会自动把这本书归还。
    public:
        Guard() = default;
        Guard(Mysql_con_pool *pool,int idx,MYSQL *con)
            :pool_(pool),idx_(idx),con_(con){}
        ~Guard();
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
        Guard(Guard &&o) noexcept;//承诺不抛异常，这些是移动构造函数
        Guard &operator=(Guard &&o) noexcept;

        bool valid() const { return con_ != nullptr; } //判断是否能够借到连接
        MYSQL *get() const { return con_; } //返回具体的数据库连接文件描述符
        bool reconnect();
        void release();

    private:
        Mysql_con_pool *pool_ = nullptr;//连接池的地址，表示这是属于哪一个连接池的假条
        int idx_ = -1;//用的是连接池下标的第几号
        //以及用的是哪条连接
        MYSQL *con_ = nullptr;
    };

    static Mysql_con_pool *Ret_Entry();
    Mysql_con_pool(const Mysql_con_pool &) = delete;
    Mysql_con_pool &operator=(const Mysql_con_pool &) = delete;

    bool Read_Conf(const std::string &path = "");
    bool Init_Con_vector();//先分配槽位，再分配坑位，也就是说，这个函数的作用是先开辟一块空间的，但是并不会初始化，也就是并不会分配与数据库之间的连接。
    bool Con_sql();//这个函数就是专门在已经分好的坑位中进行一个数据库连接符的创建。
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
    std::queue<int> idle_;//当前空余连接的队列，也就是连接池里面空闲连接的可领取列表
    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif
