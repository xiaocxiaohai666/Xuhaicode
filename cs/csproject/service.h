#include <stdlib.h>

#include <string>
#include <cstring>


#include <netinet/in.h>
#include <arpa/inet.h>

#include <jsoncpp/json/json.h>
#include <mysql/mysql.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>

#include <unistd.h>


#define MAXLINE 256
#define START -1
#define Conf_path "/home/xiaoc/mycode/cs/csproject/service.conf" 
#define Sql_Conf_path "/home/xiaoc/mycode/cs/csproject/mysql.conf"

#define d_size  1024

#define thread_num 5

#define o "ok"

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

typedef struct{
    Socket* task;
}task_t;

class Thread_Pool{
private:
    task_t * dequeue;
    int dequeue_size;
    int front;
    int rear;

    int task_Num;
    int thread_Num;

    bool status;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    pthread_t Thread_array[thread_num];
public:
     Thread_Pool();
     ~Thread_Pool();
     bool Init_Thread_pool();
     void add_Task(Socket* csocket);
     void Create_Thread();
     void Work();
};

void* General_Thread(void* arg);


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
      MYSQL mysql_con;//mysql的连接句柄

public:
     Mysql_Client(struct Mysql_config * Sql_config);
     ~Mysql_Client();

     bool Connect_Mysql_Server();
     bool Db_User_Register(const std::string &user_tel,const std::string &user_name,const std::string &user_passwd);
     bool Db_User_Login(const std::string& tel,std::string& name,const std::string& passwd);
     bool Db_Show_Ticket(Json::Value &res);
     bool Db_Yd_Ticket(std::string usertel,std::string tk_id);
     bool Db_Show_My_Ticket(const std::string&user_tel,Json::Value &res);
     bool Db_Cancel_Ticket(const std::string &user_tel,const std::string &yd_id);

     void Mysql_Begin();
     void Mysql_RollBack();
     void Mysql_Commit();

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