#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <sys/epoll.h>
#include <pthread.h>
#include <jsoncpp/json/json.h>
#include<mysql/mysql.h>



using namespace std;

struct SerConfig
{
    void ReadConfig(const char *path) // 读取配置文件
    {
        FILE *fp = fopen(path, "r");
        if (fp == nullptr)
        {
            return;
        }

        char line[256] = {0};
        int num = 0;
        /*1. 先看配置文件真实存储样子
比如你配置文件写三行：
plaintext
ip=127.0.0.1
port=3306
user=root
在磁盘里真实存的是：
plaintext
ip=127.0.0.1\nport=3306\nuser=root\n
*/
        while (fgets(line, 254, fp) != NULL)
        //fgets函数中间的参数，是读取字符数的最大值
        //它不是一次性读取254个字符，而是一个一个读直到换行符或文件结束，或者读取满了254个字符。
        //fgets函数读取到换行符，读取完换行符后（换行符也会进入到数组line里面），
        //会自动添加一个'\0'字符作为字符串的结尾，并且跳到下一行的开头位置。
        //fgets函数会返回读取到的字符数，包括换行符
        {
            num++;
            if (line[0] == '#' || line[0] == '\n')//前面的行可能是注释，可能是空行，所以要跳过
            {
                continue;
            }

            char *k = strtok(line, " ");
            char *v = strtok(NULL, " ");
            if (k == nullptr || v == nullptr)//如果k或v为空，说明配置信息格式错误，要跳过
            {
                continue;
            }

            v[strlen(v)] = 0;

            if (strcmp("ip", k) == 0)
            {
                ip = v;
            }
            else if (strcmp("port", k) == 0)
            {
                port = atoi(v);
            }
            else if (strcmp("lismax", k) == 0)
            {
                lismax = atoi(v);
            }
            else
            {
                cout << "line:" << num << " 配置信息不识别" << endl;
            }
            memset(line, 0, 256);
        }

        fclose(fp);
    }

    void PrintConfig()
    {
        cout << "----Server Config------" << endl;
        cout << "ip: " << ip << endl;
        cout << "port: " << port << endl;
        cout << "----------------------" << endl;
    }
    string ip = "127.0.0.1";
    short port = 6000;
    short lismax = 128;
};

// 操作数据库的类
class MysqlClient
{
public:
    MysqlClient()
    {
        db_ip = "127.0.0.1";
        db_port = 3306;
        db_name = "c2411db";
    }

    ~MysqlClient()
    {
        mysql_close(&mysql_con);
    }
    bool Connect_MysqlServer();
    bool Db_User_Register(const string &user_tel, const string &user_name, const string &user_passwd); // 注册数据库的函数
    bool Db_User_Login(const string &user_tel, string &name, const string &user_passwd);
    bool Db_Show_Ticket(Json::Value &res); // 查询可预约的信息，发送给客户端
    bool Db_Yd_Ticket(string user_tel,string tk_id);
    bool Db_Show_My_Ticket(const string &user_tel, Json::Value &res); // 查询我的预约信息
    bool Db_Cancel_Ticket(const string &user_tel, const string &yd_id); // 取消预定


    void Mysql_Begin();//事务
    void Mysql_Rollback();
    void Mysql_Commit();


    
private:
    string db_ip;
    short db_port;
    string db_name;

    MYSQL mysql_con; // mysql的连接句表，它里面包含了与数据库客户端的的一个套接字变量，所以本质上来说，服务器端还得与数据库的客户端去进行链接。
};

class Socket;

// 任务
typedef struct
{
    Socket *con;
} task_t;

// 线程池类 (包含 任务队列)
/*线程池的意义就像去政府大厅办事，一开始只有一个主线程窗口，
什么事都接但同一时间只能处理一件，前一个没办完后面的任务根本插不进去。
开多线程就相当于把业务拆分，设了多个对应不同职能的服务窗口。
可窗口总数有限，遇上大量办事的人同时涌来，既不能临时无限加开窗口徒增成本，
也不能直接把人赶走拒掉任务，
所以就用任务队列让大家排队取号，不承诺立刻办完，
靠公平排队让空闲窗口按顺序逐个处理任务，这就是线程池复用线程、靠队列缓冲削峰的核心逻辑。
*/
class ThreadPool
{
public:
    ThreadPool(int num, int task_max) : thread_num(num), max_queue(task_max)
    {
    }

    bool Thread_Pool_Init();
    void Add_Task(Socket *csocket); // 任务队列添加描述符（处理描述符的类对象指针替代)
    void Start_Thread();
    void Work(); //

private:
    task_t *queue; // 任务队列
    int front;     // 队头指针
    int rear;      // 队尾指针
    int count;     // 当前任务数量

    int stop; // 线程池停止

    int max_queue = 1024; // 任务队列最大长度
    int thread_num = 4;   // 线程数目

    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

// 基类
class Socket
{
public:
    Socket(int fd, int epfd) : m_fd(fd), m_epfd(epfd)
    {
    }
    ~Socket()
    {
        close(m_fd);
    }

    virtual void DoClient();

    int m_fd;
    int m_epfd;
};

// 派生类 :处理监听套接字
class LisSocket : public Socket
{
public:
    LisSocket(int fd, int epfd) : Socket(fd, epfd)
    {
    }
    void DoClient(); // 接受客户端连接 accept

private:
    void Incr()
    {
        cout << "accept client:" << ++m_Count << endl;
    }
    void RsetEvent();
    static unsigned int m_Count;
};
// 派生类:处理连接套接字
class ConSocket : public Socket
{
public:
    ConSocket(int fd, int epfd) : Socket(fd, epfd)
    {
    }
    void DoClient(); // 接收客户端数据 recv
private:
    void Send_Ok();
    void Send_Err();
    void RsetEvent();
    int Get_OpType(char buff[]);
    void User_Register();
    void User_Login();
    void Show_Ticket();
    void Yd_Ticket();
    void Show_My_Ticket();
    void Cancel_Ticket();
private:
    Json::Value m_val; //
};

class TcpServer
{
public:
    TcpServer(const SerConfig &conf);
    void Run();

    ~TcpServer()
    {
        close(m_sockfd);
    }

private:
    bool Socket_Init(); // 创建监听套接字

    void do_event();

private:
    SerConfig m_conf; // 配置文件读取的内容

    int m_sockfd = -1; // 监听套接字
    int m_epfd = -1;   // epoll 内核事件表id

    struct epoll_event m_evs[10]; // 存放就绪描述符
    int ev_num = -1;

    ThreadPool m_pool; // 线程池对象
};