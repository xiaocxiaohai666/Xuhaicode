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

#define IPADRESS "127.0.0.1"
#define PORTADRESS 9999
#define START -1
#define Length 1024
#define k "ok"
#define BUSY "MYSQL CLIENT BUSSY"   // 服务器连接池满时返回的标记

class Client{
   private:
     
     short Port;

     int conn_fd;
     int User_Login_way;
     
     bool Running;
     bool Dl_Status;
     

     std::string IP;
     std::string user_name;
     std::string user_tel;

    public:
     Client();
     ~Client();
     void Client_Init();
     void Print_Config();

     void Run();
     void Show_Menu();
     void User_Register();
     void User_login();
   
     void Show_Ticket();  
     void Yd_Ticket();
     void Show_My_Ticket();
     void Qx_Ticket();
};