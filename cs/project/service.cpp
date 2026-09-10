#include "service.h"

unsigned int LisSocket::m_Count = 0;
enum OP_TYPE
{
    ERR = 0,
    DL = 1,
    ZC,
    CKYY,
    YD,
    YDXX,
    QXYD,
    TC
};

// 线程函数
void *worker_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pool->Work();

    return NULL;
}

bool ThreadPool::Thread_Pool_Init()
{
    queue = (task_t *)malloc(sizeof(task_t) * max_queue);
    if (queue == nullptr)
    {
        return false;
    }

    // 队头 ，队尾 初始化
    front = 0;
    rear = 0;
    count = 0;

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    stop = 0; // 不退出，1退出线程池
    return true;
}

void ThreadPool::Add_Task(Socket *csocket) // 任务队列添加描述符（处理描述符的类对象指针替代)
{
    pthread_mutex_lock(&mutex);
    if (count >= max_queue)
    {
        delete csocket; // 如果队列满，销毁该描述符对应的ConSocket类的对象，析构函数close描述符
        pthread_mutex_unlock(&mutex);
        return;
    }

    queue[rear].con = csocket; // 放入任务队列，队尾插入
    rear = (rear + 1) % max_queue;
    count++; // 任务数量加一

    // 唤醒一个线程
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}


void ThreadPool::Start_Thread()
{
    pthread_t ids[thread_num]; // 测试，
    for (int i = 0; i < thread_num; i++)
    {
        pthread_create(&ids[i], NULL, worker_thread, this);
    }
}


void ThreadPool::Work()
{
    while (true)
    {
        pthread_mutex_lock(&mutex);
        while (count == 0 && !stop)
        {
            pthread_cond_wait(&cond, &mutex);
        }

        if (stop)
        {
            break;
        }

        Socket *con = queue[front].con; // LisSocket* ,ConSocket*
        front = (front + 1) % max_queue;
        count--; // 任务数量减一

        pthread_mutex_unlock(&mutex);

        // 业务代码
        if (con != nullptr)
        {
            con->DoClient();
        }
    }

    pthread_mutex_unlock(&mutex);
}

//mysql
bool MysqlClient::Connect_MysqlServer(){
   MYSQL * mysql =mysql_init(&mysql_con);
   //传入的地址和返回的地址是一样的，本质上它是给事先准备好的空句柄进行赋值操作。
   if(mysql==nullptr){
    return false;
   }
   
   mysql = mysql_real_connect(mysql,db_ip.c_str(),"root","111111",db_name.c_str(),db_port,NULL,0);//一切的数据库的操作都是基于这个mysql 类型的变量。
   if(mysql==nullptr){
    return false;
   }

   return true;
}

 bool MysqlClient::Db_User_Register(const string &user_tel,const string &user_name,const string &user_passwd){
    //insert into user_info values(0,'13318158925','小陈','123457','1',CURDATE());
    string sql = string("insert into user_info values(0,'")+user_tel+string("','")+user_name+string("','")+user_passwd+string("',1,CURDATE())");
    //mysql_query是mysql库中的执行语句函数
    if(mysql_query(&mysql_con,sql.c_str())!=0){
        cout<<"注册用户失败"<<endl;
        return false;
    }
    
    return true;
 }
 //注册数据库的函数

 bool MysqlClient::Db_User_Login(const string& tel,string &name,const string &passwd){
    string sql = string("select Name,Passwd from user_info where Tel='")+tel+string("'");
    if(mysql_query(&mysql_con,sql.c_str()) !=0){//这是一个执行函数，执行基本的增，删，改，查的sql语句
        return false;
    }

    MYSQL_RES *r =mysql_store_result(&mysql_con);//获取Myslq语句查询的结果，因为我们把语句给数据库操作之后，结果仍然在数据库那边，我们这个函数就是获取这个还在数据库中的结果，如果为nullptr，则没有查询到
    //获取到了，则结果都在MYSQL_RES 这个指针里面统一管理
    if(r==nullptr){//这里出现为空的原因是因为没有结果为空
        return false;
    }

    int num = mysql_num_rows(r);//获取这个r存储结果的行数
    if( num ==0 )//查询正常，虽结果，但是0行，即没有想要的数据
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(r);//获取查询到的用户名和密码 row[0]用户名 row[1]密码，获取到的结果放在数组里面去进行存储
    if(row == nullptr){ //
        
        return false;
    }

     string username = row[0];
     string userpasswd = row[1];
 
    mysql_free_result(r);//释放结果集占用的内存
     if(userpasswd != passwd){
        return false;
    }

    name =username;

    return true;
 
 }
bool MysqlClient::Db_Show_Ticket(Json::Value &res){
    //查询可预约的信息，发送给客户端
     string sql = "select * from ticket_table where status=1";//可以根据具体的业务需求来设计这个sql语句，查询出可预约的信息，status=1表示可预约
     if(mysql_query(&mysql_con,sql.c_str())!=0){//如果执行成功为0，失败则没有0。
        cout<<"查询可预约信息失败"<<endl;
        return false;
     }

     MYSQL_RES *r = mysql_store_result(&mysql_con);
     if(r==nullptr){
        cout<<"查询可预约信息失败"<<endl;
        return false;
     }

     int num = mysql_num_rows(r);
     res["status"] = "OK";
     res["num"] = num;
     for(int i=0;i<num;i++){
        MYSQL_ROW row = mysql_fetch_row(r);
        Json::Value val;
        val["ticket_id"] = atoi(row[0]);
        val["ticket_name"] = row[1];
        val["ticket_max"] = row[2];
        val["ticket_count"] = row[3];
        val["day_time"] = row[4];
        res["ticket_arr"].append(val);//Json对象也可添加。
     }

     mysql_free_result(r);
     return true;
}

bool MysqlClient::Db_Yd_Ticket(string usertel, string tk_id)
{
    Mysql_Begin();

    // 原子自增 + 条件判断：数据库行锁保证并发下不会读到相同 count
    string sql_update = string("update ticket_table set count=count+1 where Tk_id=")+tk_id+string(" and count < tk_max");
    if( mysql_query(&mysql_con,sql_update.c_str()) != 0)
    {
        Mysql_Rollback();
        return false;
    }

    // 受影响行数=0 说明 Tk_id 不存在或票已售罄
    if( mysql_affected_rows(&mysql_con) == 0 )
    {
        Mysql_Rollback();
        return false;
    }

    //insert into yd_table values(0,'13700000001',1,now());
    string sql_yd = string("insert into yd_table values(0,'")+usertel+string("',")+tk_id+string(",now())");
    if( mysql_query(&mysql_con,sql_yd.c_str()) != 0)
    {
        Mysql_Rollback();
        return false;
    }

    Mysql_Commit();
    return true;
}

void MysqlClient::Mysql_Begin(){
    if(mysql_query(&mysql_con,"begin")!=0){
       cout<<"事务启动失败"<<endl;  
    }
}
void MysqlClient::Mysql_Rollback(){
   if(mysql_query(&mysql_con,"rollback")!=0){
    cout<<"事务回滚失败"<<endl;
   }
}
    
void MysqlClient::Mysql_Commit(){
  if(mysql_query(&mysql_con,"commit")!=0){
    cout<<"事务提交失败"<<endl;
   }
}
void Socket::DoClient()
{
    cout << "Socket DoClient" << endl;
}

void LisSocket::DoClient()
{
    // cout<<"LisSocket DoClient"<<endl;

    int c = accept(m_fd, NULL, NULL);
    if (c < 0)
    {
        return;
    }

    Incr(); // 计数，并打印

    RsetEvent();

    ConSocket *ptr = new ConSocket(c, m_epfd);
    if (ptr == nullptr)
    {
        cout << "create ConSocket err" << endl;
        return;
    }

    struct epoll_event ev;
    ev.data.ptr = ptr; // fd
    ev.events = EPOLLIN | EPOLLONESHOT;

    if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, c, &ev) == -1)
    {
        cout << "LisSocet:epll ctl add err" << endl;
    }
}
void LisSocket::RsetEvent()
{
    struct epoll_event ev;
    ev.data.ptr = this;
    ev.events = EPOLLIN | EPOLLONESHOT;

    if (epoll_ctl(m_epfd, EPOLL_CTL_MOD, m_fd, &ev) == -1)
    {
        cout << "reEvent err" << endl;
    }
}


void ConSocket::Send_Ok()
{
    Json::Value val;
    val["status"] = "OK";
    send(m_fd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);
}
void ConSocket::Send_Err()
{
    Json::Value val;
    val["status"] = "ERR";
    send(m_fd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);
}


int ConSocket::Get_OpType(char buff[])
{
    Json::Reader read;
    if (!read.parse(buff, m_val))
    {
        cout << "json解析失败" << endl;
        return ERR;
    }

    int type = m_val["type"].asInt();
    return type;
}

void ConSocket::User_Register()
{
    //连接数据库，存用户信息，
    //如何设计数据库中的表用来存储用户信息，用来存储用户的内容
    //
    MysqlClient cli;
    if(!cli.Connect_MysqlServer()){
        cout<<"连接数据库失败"<<endl;
        Send_Err();
        return;
    }

     string tel = m_val["user_tel"].asString();
     string name = m_val["user_name"].asString();
     string passwd = m_val["user_passwd"].asString();    
    if(!cli.Db_User_Register(tel,name,passwd)){
        cout<<"注册用户失败"<<endl;
        Send_Err();
        return;
    }

    Send_Ok();

}

void ConSocket::User_Login()
{
     //连接数据库，验证用户信息
     string user_tel = m_val["user_tel"].asString();
     string user_passwd = m_val["user_passwd"].asString();

     MysqlClient cli;
     if(!cli.Connect_MysqlServer()){
          Send_Err();
          return ;
     }

     string name;//获取数据库存放的用户名

     if(!cli.Db_User_Login(user_tel,name,user_passwd)){
        Send_Err();
        return ;
     }

     Json::Value val;
     val["status"] = "OK";
     val["user_name"] = name;

     send(m_fd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

}
void ConSocket::Show_Ticket()
{
    //连接数据库，查询可预约的信息，发送给客户端
     Json::Value res;
     MysqlClient cli;
     if(!cli.Connect_MysqlServer()){
       Send_Err();
       return ;
     }
     if(!cli.Db_Show_Ticket(res)){
        Send_Err();
        return ;
     }
     send(m_fd,res.toStyledString().c_str(),strlen(res.toStyledString().c_str()),0);
}

void ConSocket::Yd_Ticket(){
  string user_tel = m_val["user_tel"].asString();
  string tk_id = m_val["ticket_id"].asString();

  MysqlClient cli;
  if(!cli.Connect_MysqlServer()){
    Send_Err();
    return ;
  }

  if(!cli.Db_Yd_Ticket(user_tel, tk_id)){
    cout<<"预约失败"<<endl;
    Send_Err();
    return ;
  }

  Send_Ok();
}

// 查询我的预约信息
bool MysqlClient::Db_Show_My_Ticket(const string &user_tel, Json::Value &res){
    // 查询该用户的所有预约，并关联 ticket_table 显示详细信息
    string sql = string("select y.yd_id, t.tk_name, t.tk_max, t.`count`, t.DateTime ")
                 + string("from yd_table y, ticket_table t ")
                 + string("where y.user_tel='") + user_tel + string("' and y.tk_id=t.Tk_id");
    if(mysql_query(&mysql_con, sql.c_str()) != 0){
        cout<<"查询我的预约失败: "<<mysql_error(&mysql_con)<<endl;
        return false;
    }

    MYSQL_RES *r = mysql_store_result(&mysql_con);
    if(r == nullptr){
        cout<<"查询我的预约结果为空"<<endl;
        res["status"] = "OK";
        res["num"] = 0;
        return true;
    }

    int num = mysql_num_rows(r);
    res["status"] = "OK";
    res["num"] = num;
    for(int i = 0; i < num; i++){
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

// 取消预定
bool MysqlClient::Db_Cancel_Ticket(const string &user_tel, const string &yd_id){
    // 先查出预约记录对应的 tk_id，以便回滚票数
    string sql_select = string("select tk_id from yd_table where yd_id=") + yd_id
                        + string(" and user_tel='") + user_tel + string("'");
    if(mysql_query(&mysql_con, sql_select.c_str()) != 0){
        cout<<"查询预约记录失败: "<<mysql_error(&mysql_con)<<endl;
        return false;
    }

    MYSQL_RES *r = mysql_store_result(&mysql_con);
    if(r == nullptr){
        return false;
    }

    int num = mysql_num_rows(r);
    if(num == 0){
        mysql_free_result(r);
        return false; // 预约记录不存在，无法取消
    }

    MYSQL_ROW row = mysql_fetch_row(r);
    string tk_id = row[0];
    mysql_free_result(r);

    Mysql_Begin();

    // 1. 删除预约记录
    string sql_del = string("delete from yd_table where yd_id=") + yd_id;
    if(mysql_query(&mysql_con, sql_del.c_str()) != 0){
        cout<<"删除预约记录失败: "<<mysql_error(&mysql_con)<<endl;
        Mysql_Rollback();
        return false;
    }

    // 2. 回滚票数（count - 1）
    string sql_update = string("update ticket_table set `count`=`count`-1 where Tk_id=") + tk_id;
    if(mysql_query(&mysql_con, sql_update.c_str()) != 0){
        cout<<"回滚票数失败: "<<mysql_error(&mysql_con)<<endl;
        Mysql_Rollback();
        return false;
    }

    Mysql_Commit();

    // 重置自增ID：删除后让新预约编号重新开始
    // 注意：ALTER TABLE 会隐式提交并加表级锁，高并发下有性能影响，学习项目可接受
    string sql_reset = "ALTER TABLE yd_table AUTO_INCREMENT = 1";
    if(mysql_query(&mysql_con, sql_reset.c_str()) != 0){
        cout<<"重置自增ID警告: "<<mysql_error(&mysql_con)<<endl;
        // 仅警告，不影响取消预约的成功返回
    }

    return true;
}

void ConSocket::Show_My_Ticket(){
    string user_tel = m_val["user_tel"].asString();

    MysqlClient cli;
    if(!cli.Connect_MysqlServer()){
        Send_Err();
        return ;
    }

    Json::Value res;
    if(!cli.Db_Show_My_Ticket(user_tel, res)){
        Send_Err();
        return ;
    }

    send(m_fd, res.toStyledString().c_str(), strlen(res.toStyledString().c_str()), 0);
}

void ConSocket::Cancel_Ticket(){
    string user_tel = m_val["user_tel"].asString();
    string yd_id = m_val["yd_id"].asString();

    MysqlClient cli;
    if(!cli.Connect_MysqlServer()){
        Send_Err();
        return ;
    }

    if(!cli.Db_Cancel_Ticket(user_tel, yd_id)){
        cout<<"取消预约失败"<<endl;
        Send_Err();
        return ;
    }

    Send_Ok();
}
void ConSocket::DoClient()
{
    char buff[1024] = {0};
    int n = recv(m_fd, buff, 1023, 0);
    if (n <= 0)
    {
        cout << "client close" << endl;
        delete this;
    }
    else
    {
        cout << "buff=" << buff << endl;
        int op = Get_OpType(buff);
        switch (op)
        {
        case ZC:
            User_Register();
            break;
        case DL:
            User_Login();
            break;
        case CKYY:
            Show_Ticket();
            break;
        case YD:
            Yd_Ticket();
            break;
        case YDXX:
            Show_My_Ticket();
            break;
        case QXYD:
            Cancel_Ticket();
            break;
        default:
            cout << "无效操作" << endl;
            Send_Err();
            break;
        }
        // 重置
        RsetEvent();
    }
}

void ConSocket::RsetEvent()
{
    struct epoll_event ev;
    ev.data.ptr = this;
    ev.events = EPOLLIN | EPOLLONESHOT;

    if (epoll_ctl(m_epfd, EPOLL_CTL_MOD, m_fd, &ev) == -1)
    {
        cout << "reEvent err" << endl;
    }
}

TcpServer::TcpServer(const SerConfig &conf) : m_conf(conf), m_pool(4, 1024)
{
}


bool TcpServer::Socket_Init()
{
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == m_sockfd)
    {
        return false;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(m_conf.port);
    saddr.sin_addr.s_addr = inet_addr(m_conf.ip.c_str());

    int res = bind(m_sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (-1 == res)
    {
        return false;
    }

    res = listen(m_sockfd, m_conf.lismax);
    if (-1 == res)
    {
        return false;
    }

    return true;
}

void TcpServer::do_event()
{
    for (int i = 0; i < ev_num; i++)
    {
        m_pool.Add_Task((Socket *)m_evs[i].data.ptr);
    }
}

void TcpServer::Run()
{
    if (!Socket_Init())
    {
        return;
    }

    m_epfd = epoll_create1(0); // 创建内核事件表,0标志位
    if (-1 == m_epfd)
    {
        return;
    }

    m_pool.Thread_Pool_Init();
    m_pool.Start_Thread();

    // 创建监听套接字类的对象
    LisSocket *sock = new LisSocket(m_sockfd, m_epfd);
    if (sock == nullptr)
    {
        cout << "create LisSocket err\n"
             << endl;
        return;
    }

    struct epoll_event ev;
    ev.data.ptr = sock;
    ev.events = EPOLLIN | EPOLLONESHOT;
    if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_sockfd, &ev) == -1)
    {
        cout << "epoll ctl add err" << endl;
        return;
    }

    while (1)
    {
        ev_num = epoll_wait(m_epfd, m_evs, 10, 5000); // 检测m_sockfd上的就绪事件
        if (ev_num == -1)
        {
            cout << "epoll wait err\n"
                 << endl;
        }
        else if (ev_num == 0)
        {
            cout << "time out" << endl;
        }
        else
        {
            do_event();
        }
    }
}

// ./server  s.conf
int main(int argc, char *argv[])
{
    const char *confpath = "ser.conf";
    if (argc > 1)
    {
        confpath = argv[1]; // 用户指定配置文件名称，配置文件的路径+名称
    }

    SerConfig config;
    config.ReadConfig(confpath);
    config.PrintConfig();

    TcpServer ser(config);
    ser.Run();

    return 0;
}