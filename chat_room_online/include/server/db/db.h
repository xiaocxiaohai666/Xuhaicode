#ifndef DB_H
#define DB_H

#include<mysql/mysql.h>
#include<atomic>
#include<string>

enum OPERA_MYSQL{
   add = 1,
   del,
   update,
   query
};

//我们这里的设计逻辑是，连接池的大小大于等于子线程的数量，每一个数据库的连接句柄由一个子线程唯一对应，不可更改，
//通过数组查询的o（1）特性，抛弃掉之前老项目的看到“有空位就做的思路"，去掉轮询，提高运行效率。
struct Mysql_cli{
   MYSQL* mysql_con;//连接的数据库句柄
   bool status;//是否被占用
   //这里原先有一个变量是专门存储这个对应的线程id，但是，由于mudlo的线程池本质上也是由vector实现的，
   //那么我们设计的时候，就采用相同下标为一个小单元，实现o(1)的连接池下标访问。
};


//固定式连接池的设计
//设计成单例模式
class Mysql_con_pool{
    private:
        //数据库ip地址：
        std::string IP;
        //数据库端口号：
        int Port = 0;

        //默认登录账户名：
        std::string user;
        //登录密码：
        std::string Passwd;
        //登录数据库名：
        std::string Database_name;
        //字符集
        std::string char_set;

        //连接池数组
        Mysql_cli* C_P_array;

        //连接池的最大连接数，也就是最大的容量
        int Max_User = 0;
        //使用数量，也就是已经被线程领走的下标个数
        //必须用原子变量，多个 IO 线程会同时来分配下标
        static std::atomic<int> User_now;
        //状态
        bool status;

        //构造函数
        Mysql_con_pool();
    public:
       //单例模式下的静态函数接口
        static Mysql_con_pool* Ret_Entry();
        Mysql_con_pool(const Mysql_con_pool &)=delete;
        Mysql_con_pool& operator=(const Mysql_con_pool &)=delete;
        
        //读取配置文件；path 为空时使用编译期注入的 conf 目录
        bool Read_Conf(const std::string &path = "");

        //初始化连接池数组
        bool Init_Con_vector();

        //析构函数
        ~Mysql_con_pool();
    
        //连接函数
        bool Con_sql();

        //增
        bool Add_to_sql(const std::string msg);
        
        //删
        bool Del_to_sql(const std::string msg);

        //改
        bool Upd_to_sql(const std::string msg);

        //查
        MYSQL_RES* Que_to_sql(const std::string msg);
        
        //返回句柄
        MYSQL* get_connection();

};


#endif