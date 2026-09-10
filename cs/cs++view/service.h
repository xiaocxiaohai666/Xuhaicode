#include <cstdlib>

#include <string>
#include <cstring>

// C++11 线程库替代 pthread
#include <thread>
#include <mutex>
#include <condition_variable>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <jsoncpp/json/json.h>
#include <mysql/mysql.h>

#include <sys/socket.h>
#include <sys/epoll.h>

#include <unistd.h>


#define MAXLINE 256
#define START -1
#define Conf_path "/home/xiaoc/mycode/cs/cs++view/service.conf"
#define Sql_Conf_path "/home/xiaoc/mycode/cs/cs++view/mysql.conf"

#define d_size  1024

#define thread_num 5

#define o "ok"

class Connect_Pool;
extern Connect_Pool* g_pool;

struct Service_Config{
     const char* config_path=Conf_path;
     std::string IP;
     short Port;
     short Lismax;


     void ReadConfig(const char* config_path);
     void PrintConfig() const;
};




class Socket{
public:
      int fd;
      int epfd;

      Socket(int fd,int epfd);
     virtual ~Socket();
     virtual void Do_client() = 0;
};

class AccSocket : public Socket{
public:
      AccSocket(int fd,int epfd);
      ~AccSocket();
     void Do_client() override;

private:
     static unsigned int count;
};

class RecvSocket : public Socket{
public:
      RecvSocket(int fd,int epfd);
      ~RecvSocket();
      void Do_client() override;
      void send_Ok();
      void send_false();

};

// C++ 风格：struct 不再需要 typedef
struct task_t{
    Socket* task;
};

class Thread_Pool{
private:
    task_t * dequeue;
    int dequeue_size;
    int front;
    int rear;

    int task_Num;
    int thread_Num;

    bool status;

    std::mutex mutex;                       // 替代 pthread_mutex_t
    std::condition_variable cond;           // 替代 pthread_cond_t
    std::thread Thread_array[thread_num];   // 替代 pthread_t[]

public:
     Thread_Pool();
     ~Thread_Pool();
     bool Init_Thread_pool();
     void add_Task(Socket* csocket);
     void Create_Thread();
     void Work();
};

// std::thread 不需要 void* 参数，直接传 Thread_Pool*
void General_Thread(Thread_Pool* pool);


struct Mysql_config{
     short db_port;
     std::string db_ip;
     std::string db_name;
     const char* Sql_config_path=Sql_Conf_path;

     void Init_Mysql_config(const char* Sql_config_path);
     void print_Mysql_config () const;
};

class Mysql_Client{
private:
      struct Mysql_config* file;
      MYSQL* mysql_con;//mysql的连接句柄
      int who;
      bool connected;   // 是否成功从池里借到连接
public:
     Mysql_Client(struct Mysql_config * Sql_config,int who);
     void ResetSTatus();
     ~Mysql_Client();
     bool IsConnected() const { return connected; }   // 供外部检查是否借到连接

     bool Connect_Mysql_Server();
     bool Db_User_Register(const std::string &user_tel,const std::string &user_name,const std::string &user_passwd);
     bool Db_User_Login(const std::string& tel,std::string& name,const std::string& passwd);
     bool Db_Show_Ticket(Json::Value &res);
     bool Db_Yd_Ticket(std::string usertel,std::string tk_id);
     bool Db_Show_My_Ticket(const std::string&user_tel,Json::Value &res);
     bool Db_Cancel_Ticket(const std::string &user_tel,const std::string &yd_id);

     void Send_full();
     void Mysql_Begin();
     void Mysql_RollBack();
     void Mysql_Commit();

};

//连接池中的每一个与数据库进行连接的客户端入口结构体
struct Mysql_cli{
     MYSQL* mysql_con;
     bool status;
};



class Connect_Pool{
private:

   bool status;
   int MAX_USER;
   Mysql_config* Sql_config;
public:
   Mysql_cli* C_P_array;
   int user;
   std::mutex mutex;   // 替代 pthread_mutex_t


   Connect_Pool(Mysql_config* Sql_config);
   ~Connect_Pool();

   bool Run();
   void Reset();
};

class Tcp_Service{
private:
     const Service_Config* config;
     Thread_Pool* pool;

     int lis_fd = START;
     int ep_fd = START;
     bool Status;

     struct epoll_event ready_events[MAXLINE]{0};


public:
     Tcp_Service(const Service_Config* config);
     ~Tcp_Service();

     void Init(const Service_Config* config);
};
