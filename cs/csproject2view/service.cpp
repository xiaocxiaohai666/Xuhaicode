#include "service.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <iostream>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <mysql/mysql.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace std;

Mysql_config file;
Connect_Pool* g_pool = nullptr;

enum OP_TYPE {
  Login = 1,
  Register,
  CheckApoint,
  Appointment,
  AppoinMessage,
  CancelAppoin,
  Exit
};

void Service_Config::ReadConfig(const char *config_path) {
  if (config_path == nullptr) {
    cout << "config_path is nullptr!" << endl;
    return;
  }

  FILE *fp = fopen(config_path, "r");
  if (fp == nullptr) {
    perror("fopen config file failed");
    cout << "fopen config file failed! errno=" << errno << endl;
    return;
  }

  char line[MAXLINE]{0};
  int num = 0;

  while (fgets(line, MAXLINE - 2, fp) != nullptr) {
    num++;
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }

    char *Before = strtok(line, "=");
    char *After = strtok(nullptr, "=");

    if (Before == nullptr || After == nullptr) {
      continue;
    }

    After[strlen(After) - 1] = '\0';

    if (strcmp("ip", Before) == 0) {
      IP = After; // 禁止共享内存，line是一次性使用，After会被覆盖
    } else if (strcmp("port", Before) == 0) {
      Port = atoi(After);
    } else if (strcmp("lismax", Before) == 0) {
      Lismax = atoi(After);
    } else if (strcmp("service_conf_path", Before) == 0) {
      config_path = After;
    } else {
      cout << "unknown config item: " << Before << " at line " << num << endl;
    }
    memset(line, 0, sizeof(line));
  }
  fclose(fp);
}

void Service_Config::PrintConfig() const {
  cout << "IP: " << IP << endl;
  cout << "Port: " << Port << endl;
  cout << "Lismax: " << Lismax << endl;
  cout << "config_path: " << config_path << endl;
}

Tcp_Service::Tcp_Service(const Service_Config *config) { Init(config); }

Tcp_Service::~Tcp_Service() {
  cout << "~Tcp_Service()" << endl;
  close(lis_fd);
  close(ep_fd);
  if (pool != nullptr)
    delete pool;
}

Socket::Socket(int fd, int epfd) : fd(fd), epfd(epfd) {}

Socket::~Socket() {}

AccSocket::AccSocket(int fd, int epfd) : Socket(fd, epfd) {}

unsigned int AccSocket::count = 0;

AccSocket::~AccSocket() {}

void AccSocket::Do_client() {
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  memset(&client_addr, 0, sizeof(client_addr));

  // lis_fd对accept的影响，在默认情况下，lis_fd在队列为空时，会默认让accept在获取这个空队列时阻塞，线程会直接阻塞卡死在这
  // 所以我们要用非阻塞模式的accept，当队列为空时，accept会返回EAGAIN或EWOULDBLOCK，而不是阻塞线程
  int conn_fd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);

  if (conn_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // listen 队列空了,别的 worker 已经把连接取走了,正常退出
      delete this;
      return;
    }

    perror("accept failed");
    cout << "accept failed! errno=" << errno << endl;
    delete this;
    return;
  }

  cout << "client:" << conn_fd << " connect success!" << endl;
  cout << "Service accept client numb now: " << ++count << endl;
  cout << "-------------------------------------" << endl;

  struct epoll_event ev_client_accpet;
  ev_client_accpet.events = EPOLLIN | EPOLLONESHOT;
  auto *recvSock = new RecvSocket(conn_fd, epfd);
  ev_client_accpet.data.ptr = recvSock;

  if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev_client_accpet) == -1) {
    perror("epoll_ctl add client fd failed");
    cout << "epoll_client_ctl add false! errno=" << errno << endl;
    close(conn_fd);  // 关闭连接,不受监控的不要
    delete recvSock; // 删除对象
  }

  delete this;
}

RecvSocket::RecvSocket(int fd, int pdfd) : Socket(fd, pdfd) {}

RecvSocket::~RecvSocket() { close(fd); }

void RecvSocket::send_Ok() {
  Json::Value value;
  string a = o;
  value["status"] = a;
  send(fd, value.toStyledString().c_str(),
       strlen(value.toStyledString().c_str()), 0);
}

void RecvSocket::send_false() {
  Json::Value value;
  string a = "false";
  value["status"] = a;
  send(fd, value.toStyledString().c_str(),
       strlen(value.toStyledString().c_str()), 0);
}


void RecvSocket::Do_client() {
  char buff[MAXLINE]{0};
  ssize_t recv_num = recv(fd, buff, sizeof(buff) - 1, 0);

  bool tag = false;

  if (recv_num < 0) {
    perror("recv failed");
    cout << "[fd=" << fd << "] recv 出错 errno=" << errno << endl;
    tag = true;
  } else if (recv_num == 0) {
    cout << "[fd=" << fd << "] 客户端正常关闭连接" << endl;
    tag = true;
  } else {
    buff[recv_num] = '\0';
    cout << "[fd=" << fd << "] 收到 " << recv_num << " 字节" << endl;
    cout << "---------------------------------------------" << endl;

    Json::Value val;
    Json::Reader read;
    if (!read.parse(buff, val)) {
      cout << "[fd=" << fd << "] Json 解析失败! 收到内容: " << buff << endl;
      tag = true;
    } else {
      try {
      Mysql_Client sql(&file,fd);
      if (sql.IsConnected()) {
      int i = val["type"].asInt();
      if (i == Login) {
        string user_tel = val["user_tel"].asString();
        string passwd = val["passwd"].asString();
        string user_name = val["user_name"].asString();
        if (sql.Db_User_Login(user_tel, user_name, passwd)) {
          send_Ok();
        } else {
          send_false();
        }
      } else if (i == Register) {
        string tel = val["user_tel"].asString();
        string name = val["user_name"].asString();
        string passwd = val["passwd"].asString();
        if (sql.Db_User_Register(tel, name, passwd)) {
          send_Ok();
        } else {
          send_false();
        }

      } else if (i == CheckApoint) {
        Json::Value Reader;
        if (!sql.Db_Show_Ticket(Reader)) {
          send_false();
        } else
          send(fd, Reader.toStyledString().c_str(),
               strlen(Reader.toStyledString().c_str()), 0);
      } else if (i == Appointment) {
        string user_tel = val["user_tel"].asString();
        string ticket_id = val["ticket_id"].asString();
        if (!sql.Db_Yd_Ticket(user_tel, ticket_id)) {
          send_false();
        } else {
          send_Ok();
        }
      } else if (i == AppoinMessage) {
        string user_tel = val["user_tel"].asString();
        Json::Value res;
        if (!sql.Db_Show_My_Ticket(user_tel, res)) {
          send_false();
        } else {
          send(fd, res.toStyledString().c_str(),
               strlen(res.toStyledString().c_str()), 0);
        }
      } else if (i == CancelAppoin) {
        string user_tel = val["user_tel"].asString();
        string yd_id = val["yd_id"].asString();
        if (!sql.Db_Cancel_Ticket(user_tel, yd_id)) {
          send_false();
        } else {
          send_Ok();
        }
      } else {
        tag = true;
      }
      }
      } catch (const std::exception& e) {
        cout << "[fd=" << fd << "] JSON 处理异常: " << e.what()
             << " 收到: " << buff << endl;
        tag = true;
      }
    }
  }


  if (tag) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    delete this;
  } else {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = this;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
  }
}

Thread_Pool::Thread_Pool() {
  if (!Init_Thread_pool()) {
  exit(1);
  }

  Create_Thread();
}

Thread_Pool::~Thread_Pool() {
  pthread_mutex_lock(&mutex);
  status = false;
  pthread_cond_broadcast(&cond); // 唤醒所有在 cond_wait 的线程
  pthread_mutex_unlock(&mutex);

  for (int i = 0; i < thread_num; i++) {
    pthread_join(Thread_array[i], NULL);
  }
  free(dequeue);
  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&cond);
}

bool Thread_Pool::Init_Thread_pool() {

  dequeue = (task_t *)malloc(sizeof(task_t) * d_size);

  if (dequeue == nullptr) {
    cout << "dequeue malloc failure" << endl;
    return false;
  }

  dequeue_size = d_size;
  front = 0;
  rear = front;
  task_Num = 0;
  thread_Num = thread_num;

  pthread_cond_init(&cond, NULL);
  pthread_mutex_init(&mutex, NULL);

  status = true;
  cout << "ThreadPool init success!" << endl;
  cout << "---------------------------------------" << endl;

  return true;
}

void Thread_Pool::add_Task(Socket *csocket) {
  pthread_mutex_lock(&mutex);
  if (task_Num >= dequeue_size) {
    delete csocket;
    cout << "dequeue_size is full!add_Task false!" << endl;
    pthread_mutex_unlock(&mutex);
    return;
  }

  dequeue[rear].task = csocket;
  rear = (rear + 1) % dequeue_size;
  task_Num++;

  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);
}

void *General_Thread(void *arg) {
  Thread_Pool *poo = (Thread_Pool *)arg;
  poo->Work();
  return NULL;
}

void Thread_Pool::Create_Thread() {
  for (int i = 0; i < thread_num; i++) {
    pthread_create(&Thread_array[i], NULL, General_Thread, this);
  }
}

void Thread_Pool::Work() {
  while (true) {
    pthread_mutex_lock(&mutex);

    while (task_Num == 0 && status == true) {
      pthread_cond_wait(&cond, &mutex);
      // 这是条件休眠，进入到条件变量的休眠队列，进入条件变量的休眠队列是会自动释放锁，这与sleep是不相同的。
    } // 不能用if，要用while，以防虚假唤醒。

    if (status == false) {
      cout << "ThreadPool not run....." << endl;
      break;
    } // 处理线程池未启用的状态。

    Socket *tmp = dequeue[front].task;
    if (tmp == nullptr) {
      cout << "tmp is nullptr" << endl;
      break;
    }
    front = (front + 1) % dequeue_size;
    task_Num--;
    // cout<<"Thread id: "<<pthread_self()<<endl;
    pthread_mutex_unlock(&mutex);

    tmp->Do_client();
  }
  // 发生意外情况也要及时释放锁
  pthread_mutex_unlock(&mutex);
}

void Mysql_config::Init_Mysql_config(const char *Sql_config_path) {
  if (Sql_config_path == nullptr) {
    cout << "config_path is nullptr!" << endl;
    return;
  }

  FILE *fp = fopen(Sql_config_path, "r");
  if (fp == nullptr) {
    perror("fopen config file failed");
    cout << "fopen config file failed! errno=" << errno << endl;
    return;
  }

  char line[MAXLINE]{0};
  int num = 0;

  while (fgets(line, MAXLINE - 2, fp) != nullptr) {
    num++;
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }

    char *Before = strtok(line, "=");
    char *After = strtok(nullptr, "=");

    if (Before == nullptr || After == nullptr) {
      continue;
    }

    After[strlen(After) - 1] = '\0';

    if (strcmp("dp_ip", Before) == 0) {
      db_ip = After; // 禁止共享内存，line是一次性使用，After会被覆盖
    } else if (strcmp("dp_port", Before) == 0) {
      db_port = atoi(After);
    } else if (strcmp("dp_name", Before) == 0) {
      db_name = After;
    } else if (strcmp("dp_mysql_path", Before) == 0) {
      Sql_config_path = After;
    } else {
      cout << "unknown config item: " << Before << " at line " << num << endl;
    }
    memset(line, 0, sizeof(line));
  }
  fclose(fp);
  print_Mysql_config();
}

void Mysql_config::print_Mysql_config() const {
  cout << "db_ip: " << db_ip << endl;
  cout << "db_port: " << db_port << endl;
  cout << "db_name: " << db_name << endl;
  cout << "config_path: " << Sql_config_path << endl;
}

 void Mysql_Client::Send_full(){
  string a = "MYSQL CLIENT BUSSY";
  Json::Value value;
  value["status"] = a;
  send(who, value.toStyledString().c_str(),
       strlen(value.toStyledString().c_str()), 0);
 }

Mysql_Client::
Mysql_Client(struct Mysql_config *Sql_config,int who):who(who) {
  connected = false;
  mysql_con = nullptr;
  if (Sql_config == nullptr) {
    cout << "Mysql_config_path nullptr!" << endl;
    Send_full();
    return;                       // 不 exit, 让客户端收到 busy 后自己重试
  }
  file = (struct Mysql_config *)Sql_config;
  if (!Connect_Mysql_Server()) {
    cout << "---------------------------------------------" << endl;
    cout << "connect mysql false (pool busy)" << endl;
    cout << "---------------------------------------------" << endl;
    Send_full();                  // 给客户端发 "MYSQL CLIENT BUSSY"
    return;                       // 不再继续, 后续 Db_* 也不会被调用
  }
  connected = true;
  cout << "connect mysql succeed" << endl;
  cout << "---------------------------------------------" << endl;
}


void Mysql_Client::Mysql_Begin() {
  if (!connected) return;
  if (mysql_query(mysql_con, "begin") != 0) {
    cout << "事物启动失败!" << endl;
  }
}

void Mysql_Client::Mysql_RollBack() {
  if (!connected) return;
  if (mysql_query(mysql_con, "rollback") != 0) {
    cout << "事物回滚失败!" << endl;
  }
}

void Mysql_Client::Mysql_Commit() {
  if (!connected) return;
  if (mysql_query(mysql_con, "commit") != 0) {
    cout << "事物提交失败!" << endl;
  }
}

bool Mysql_Client::Connect_Mysql_Server() {
  pthread_mutex_lock(&g_pool->mutex);
  

  int i=0;
  while(i<thread_num){
    if(g_pool->C_P_array[i].status==true)
    {
  mysql_con = g_pool->C_P_array[i].mysql_con;
  g_pool->C_P_array[i].status=false;
  break;
    }
    i++;
  }


  if(i>=thread_num){
  pthread_mutex_unlock(&g_pool->mutex);
  return false; 
  }
  else{
  g_pool->user++;
  pthread_mutex_unlock(&g_pool->mutex);  
  return true;
  }
}

bool Mysql_Client::Db_User_Register(const std::string &user_tel,
                                    const std::string &user_name,
                                    const std::string &user_passwd) {
  if (!connected) return false;

  string sql = string("insert into user_info values(0,'") + user_tel +
               string("','") + user_name + string("','") + user_passwd +
               string("',1,CURDATE())");

  if (mysql_query(mysql_con, sql.c_str()) != 0) {
    return false;
  }

  return true;
}

bool Mysql_Client::Db_User_Login(const std::string &tel, std::string &name,
                                 const std::string &passwd) {
  if (!connected) return false;
  string sql = string("select Name,Passwd from user_info where Tel='") + tel +
               string("'");
  if (mysql_query(mysql_con, sql.c_str()) != 0) {
    return false;
  }

  MYSQL_RES *r = mysql_store_result(mysql_con);
  if (r == nullptr) {
    return false;
  }

  int num = mysql_num_rows(r);
  if (num == 0) {
    return false;
  }

  MYSQL_ROW row = mysql_fetch_row(r);
  if (row == nullptr) {
    return false;
  }

  string username = row[0];
  string userpasswd = row[1];

  mysql_free_result(r);
  if (userpasswd != passwd || name != username) {
    return false;
  }

  return true;
}

bool Mysql_Client::Db_Show_Ticket(Json::Value &res) {
  if (!connected) return false;
  string sql = "select * from ticket_table where status=1";
  if (mysql_query(mysql_con, sql.c_str()) != 0) {
    return false;
  }

  MYSQL_RES *r = mysql_store_result(mysql_con);
  if (r == nullptr) {
    return false;
  }

  int num = mysql_num_rows(r);
  if (num == 0) {
    return false;
  }
  res["status"] = o;
  res["num"] = num;
  for (int i = 0; i < num; i++) {
    MYSQL_ROW row = mysql_fetch_row(r);
    Json::Value val;
    val["ticket_id"] = atoi(row[0]);
    val["ticket_name"] = row[1];
    val["ticket_max"] = row[2];
    val["ticket_count"] = row[3];
    val["day_time"] = row[4];
    res["ticket_arr"].append(val); // Json对象也可以添加进去
  }

  mysql_free_result(r);
  return true;
}

bool Mysql_Client::Db_Yd_Ticket(std::string usertel, std::string tk_id) {
  if (!connected) return false;

  Mysql_Begin();
  // 一旦开启事物，这两条sql语句都成功执行的情况下，才会去进行提交事物的操作，
  //  否则一旦有其中一个未成功，就算其中一个成功的操作也会撤销，二者立刻回滚，所有改动立即撤销，

  // 原子自增 + 条件判断：数据库行锁保证并发下不会读到相同 count
  string sql_update = string("update ticket_table set count=count+1 "
                             "where Tk_id=") + tk_id +
                      string(" and count < tk_max");
  if (mysql_query(mysql_con, sql_update.c_str()) != 0) {
    Mysql_RollBack();
    return false;
  }

  // 受影响行数=0 说明 Tk_id 不存在或票已售罄
  if (mysql_affected_rows(mysql_con) == 0) {
    Mysql_RollBack();
    return false;
  }

  string sql_yd = string("insert into yd_table values(0,'") + usertel +
                  string("','") + tk_id + string("',now())");
  if (mysql_query(mysql_con, sql_yd.c_str()) != 0) {
    Mysql_RollBack();
    return false;
  }

  Mysql_Commit();
  return true;
}

bool Mysql_Client::Db_Show_My_Ticket(const std::string &user_tel,
                                     Json::Value &res) {
  if (!connected) return false;
  string sql =
      string("select y.yd_id, t.tk_name, t.tk_max, t.`count`, y.ctime ") +
      string("from yd_table y, ticket_table t ") +
      string("where y.user_tel='") + user_tel + string("' and y.tk_id=t.Tk_id");
  if (mysql_query(mysql_con, sql.c_str()) != 0) {
    cout << mysql_error(mysql_con) << endl;
    return false;
  }

  MYSQL_RES *r = mysql_store_result(mysql_con);
  if (r == nullptr) {
    res["status"] = o;
    res["num"] = 0;
    return true;
  }

  int num = mysql_num_rows(r);
  res["status"] = o;
  res["num"] = num;
  for (int i = 0; i < num; i++) {
    MYSQL_ROW row = mysql_fetch_row(r);
    Json::Value val;
    val["yd_id"] = row[0];
    val["ticket_name"] = row[1];
    val["ticket_max"] = row[2];
    val["ticket_count"] = row[3];
    val["day_time"] = row[4];
    res["my_ticket_arr"].append(val);
  }

  mysql_free_result(r);
  return true;
}

bool Mysql_Client::Db_Cancel_Ticket(const std::string &user_tel,
                                    const std::string &yd_id) {
  if (!connected) return false;
  string sql_select = string("select tk_id from yd_table where yd_id=") +
                      yd_id + string(" and user_tel='") + user_tel +
                      string("'");

  // 因为我们删除的时候是删除我们自己的预约表的最前面的那个编号，而不是票的票号，所以我们要额外的去存储这个票的票号，对这个票的数据进行减一的操作。
  if (mysql_query(mysql_con, sql_select.c_str()) != 0) {
    cout << "查询预约记录失败: " << mysql_error(mysql_con) << endl;
    return false;
  }

  MYSQL_RES *r = mysql_store_result(mysql_con);
  if (r == nullptr) {
    return false;
  }

  int num = mysql_num_rows(r);
  if (num == 0) {
    mysql_free_result(r);
    return false; // 预约记录不存在，无法取消
  }

  MYSQL_ROW row = mysql_fetch_row(r);
  string tk_id = row[0];
  mysql_free_result(r);

  Mysql_Begin();

  string sql_del = string("delete from yd_table where yd_id=") + yd_id;
  if (mysql_query(mysql_con, sql_del.c_str()) != 0) {
    cout << mysql_error(mysql_con) << endl;
    Mysql_RollBack();
    return false;
  }

  string sql_update =
      string("update ticket_table set `count`=`count`-1 where Tk_id=") + tk_id;
  if (mysql_query(mysql_con, sql_update.c_str()) != 0) {
    cout << mysql_error(mysql_con) << endl;
    Mysql_RollBack();
    return false;
  }

  Mysql_Commit();

  string sql_reset = "ALTER TABLE yd_table AUTO_INCREMENT = 1";
  if (mysql_query(mysql_con, sql_reset.c_str()) != 0) {
    cout << mysql_error(mysql_con) << endl;
  }
  return true;
}

Mysql_Client::~Mysql_Client() {
  pthread_mutex_lock(&g_pool->mutex);
  for(int i=0;i<thread_num;i++){
    if(mysql_con==g_pool->C_P_array[i].mysql_con){
    g_pool->C_P_array[i].status=true;
    g_pool->user--;
    }
   }
   pthread_mutex_unlock(&g_pool->mutex);
}

Connect_Pool::Connect_Pool(Mysql_config *Sql_config) {
  if (Sql_config == nullptr) {
    cout << "Connect Sqlconfig == nullptr!" << endl;
    return;
  }

  user = 0;
  status = true;
  MAX_USER = thread_num;
  this->Sql_config = Sql_config;
  C_P_array = new Mysql_cli[thread_num]();   // 值初始化，避免 mysql_con 为野值导致 Reset() 段错误

  Reset();
  pthread_mutex_init(&mutex,NULL);

  if(!Run()){
    cout<<"Connect_Pool Run failed!"<<endl;
    Reset();
    exit(1);
  }
  cout<<"-------------------------"<<endl;
  cout<<"Connect_Pool Run success!"<<endl;
  cout<<"-------------------------"<<endl;
}

bool Connect_Pool::Run() {
  if (status == false) {
    return false;
  }
  for (int i = 0; i < thread_num; i++) {

    MYSQL *mysql = mysql_init(C_P_array[i].mysql_con);
    if (mysql == nullptr) {
      return false;
    }

    mysql = mysql_real_connect(mysql, Sql_config->db_ip.c_str(), "root",
                               "111111", Sql_config->db_name.c_str(),
                               Sql_config->db_port, NULL, 0);

    if (mysql == nullptr) {
      return false;
    }
    
    C_P_array[i].mysql_con = mysql;
    C_P_array[i].status = true;
  }   
  return true;
}


void Connect_Pool::Reset(){
for(int i=0;i<thread_num;i++){
  if(C_P_array[i].mysql_con!=nullptr){
    mysql_close( C_P_array[i].mysql_con);
  }
  C_P_array[i].mysql_con= nullptr;
  C_P_array[i].status = false;
}
}

Connect_Pool::~Connect_Pool(){
  for(int i = 0;i<thread_num;i++){
    mysql_close(C_P_array[i].mysql_con);
  }
  delete C_P_array;
  pthread_mutex_destroy(&mutex);
}

void Tcp_Service::Init(const Service_Config *config) {
  if (config == nullptr) {
    cout << "config is nullptr!" << endl;
    return;
  }
  this->config = config;
  Status = true;

  pool = new Thread_Pool;

  Mysql_config file;
  file.Init_Mysql_config(Sql_Conf_path);

  lis_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (lis_fd == START) {
    perror("socket failed");
    cout << "socket failed! errno=" << errno << endl;
    exit(1);
  }

  // listen socket 设为非阻塞,accept 队列空时返回 EAGAIN 而不是阻塞
  int flags = fcntl(lis_fd, F_GETFL, 0);
  fcntl(lis_fd, F_SETFL, flags | O_NONBLOCK);
  // 因为是lis_fd为空导致的阻塞，所以我们这个将模式设为非阻塞，在此之后，accept会直接返回EAGAIN或EWOULDBLOCK，而不是阻塞线程
  // 不会阻塞

  int opt = 1;
  setsockopt(lis_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(config->IP.c_str());
  server_addr.sin_port = htons(config->Port);

  config->PrintConfig();

  if (bind(lis_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("bind failed");
    cout << "bind failed! errno=" << errno << endl;
    exit(1);
  }

  if (listen(lis_fd, config->Lismax) < 0) {
    perror("listen failed");
    cout << "listen init error! errno=" << errno << endl;
    exit(1);
  }

  int epfd =
      epoll_create1(EPOLL_CLOEXEC); // 防止fork版本公用一个文件描述符的情况
  if (-1 == epfd) {
    perror("epoll_create1 failed");
    cout << "create epoll error! errno=" << errno << endl;
    close(lis_fd);
    exit(1);
  }

  ep_fd = epfd;
  cout << "epoll success!" << endl;

  struct epoll_event ev_listen;
  ev_listen.events = EPOLLIN;
  ev_listen.data.fd = lis_fd;

  if (epoll_ctl(epfd, EPOLL_CTL_ADD, lis_fd, &ev_listen) == -1) {
    perror("epoll_ctl add listen fd failed");
    cout << "epoll_ctl add false! errno=" << errno << endl;
    close(lis_fd);
    exit(1);
  }

  cout << "Tcp service success!" << endl;
  cout << "------------------------------------------------------" << endl;

  while (Status) {

    int ready_count = epoll_wait(epfd, ready_events, MAXLINE, -1);
    //- 内核 :负责把发生事件的 fd 放入就绪双链表(这是中断/回调驱动的,随时发生)
    // epoll_wait :负责从就绪双链表取出事件,拷贝给用户态的 ready_events
    if (ready_count == -1) {
      perror("epoll_wait failed");
      cout << "epoll_wait error! errno=" << errno << endl;
      close(lis_fd);
      exit(1);
    }

    for (int i = 0; i < ready_count; i++) {
      int tmfd = ready_events[i].data.fd;
      if (tmfd == lis_fd) {
        auto csocket = new AccSocket(lis_fd, epfd);
        pool->add_Task(csocket);
      } else {
        auto csocket = ready_events[i].data.ptr;
        if (csocket == nullptr) {
          cout << "csocket is nullptr" << endl;
          continue;
        }
        pool->add_Task((Socket *)csocket);
      }
    }
  }
}


int main() {
  // 忽略 SIGPIPE：客户端提前关闭连接时，send() 返回 -1 而不是杀掉 service 进程
  signal(SIGPIPE, SIG_IGN);

  file.Init_Mysql_config(Sql_Conf_path);
  file.print_Mysql_config();  
  cout << "-----------------------------------------" << endl;
  g_pool = new Connect_Pool(&file);


  Service_Config a;
  a.ReadConfig(a.config_path);
  cout << "-----------------------------------------" << endl;

  Tcp_Service tcp_service(&a);
  delete g_pool;
  return 0;
}