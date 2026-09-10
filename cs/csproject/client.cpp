#include "client.h"
#include <jsoncpp/json/value.h>
#include <ostream>
#include <sys/socket.h>
#include <iostream>
using namespace std;

enum OP_TYPE{
  Login = 1,
  Register,
  CheckApoint,
  Appointment,
  AppoinMessage,
  CancelAppoin,
  Exit
};



Client::Client()
{
    IP=IPADRESS;
    Port = PORTADRESS;
    conn_fd = START;
    Dl_Status = false;
    Running = true;
    Client_Init();
}


Client::~Client()
{
    if(conn_fd != START){
        close(conn_fd);
    }
    cout<<"Client close"<<endl;
}

void Client::Client_Init(){

   int sock_fd = socket(AF_INET,SOCK_STREAM,0);
   if(sock_fd<=0){
    cout<<"Client socket Error!"<<endl;
    exit(1);
   }

   conn_fd = sock_fd;
   Print_Config();
 

   struct sockaddr_in Client_sock_ad;
   memset(&Client_sock_ad,0,sizeof(Client_sock_ad));
   Client_sock_ad.sin_family = AF_INET;
   Client_sock_ad.sin_addr.s_addr=inet_addr(IPADRESS);
   Client_sock_ad.sin_port = htons(PORTADRESS);

   if(connect(sock_fd,(const struct sockaddr* )&Client_sock_ad,sizeof(Client_sock_ad))==START){
    cout<<"connect failed!"<<endl;
    close(sock_fd);
    exit(1);
   }

   cout<<"connect succed!"<<endl;
   cout<<"               client start begin                     "<<endl;
   cout<<"------------------------------------------------------"<<endl;

   Run();
}

void Client::Print_Config(){
    cout<<"IP: "<<IP<<endl;
    cout<<"Port: "<<Port<<endl;
    cout<<"conn_fd: "<<conn_fd<<endl;
    cout<<"this: "<<this<<endl;
}

void Client::Show_Menu(){
    if(!Dl_Status){
        cout <<"---用户名:游客---------状态：未登录-----"<<endl;
        cout << "1 登陆   2  注册   3  退出" << endl;
        cout << "---------------------------------" << endl;
        cout << "请输入选项编号:" << endl;
    }
    else{
        cout << "---用户名：" << user_name << "--------状态：已登陆------" << endl;
        cout << "1 查看预约      2  预定 " << endl;
        cout << "3 查看我的预约   4 取消预定 " << endl;
        cout << " 5 退出" << endl;
        cout << "---------------------------------" << endl;
        cout << "请输入选项编号:" << endl;
        cout << "---------------------------------" << endl;
    }
}

void Client::User_Register(){
    cout<<"请输入手机号码："<<endl;
    cin>>user_tel;
    cout<<"请输入用户名："<<endl;
    cin>>user_name;
    cout<<"请输入密码"<<endl;
    string passwd;
    cin>>passwd;

    Json::Value mess;
    mess["type"] = Register;
    mess["user_tel"] = user_tel;

    mess["user_name"] = user_name;
    mess["passwd"] = passwd;
    send(conn_fd,mess.toStyledString().c_str(),strlen(mess.toStyledString().c_str()),0);

    char buff_status[Length] = {0};
    int n = recv(conn_fd,buff_status,Length-1,0);
    if(n<0){
        cout<<"ser err"<<endl;
        return;
    }

    Json::Value Acmess;
    Json::Reader Read;
    if(!Read.parse(buff_status,Acmess)){
        cout<<"Json Read error!"<<endl;
        return;
    }

    string st = Acmess["status"].asString();
    if(st.compare(k)!=0){
        cout<<"Register false!";
        return ;
    }

    cout<<"Register success!"<<endl;
    Dl_Status = true;
}

void Client::User_login(){
    cout<<"请输入手机号码"<<endl;
    cin>>user_tel;
    cout<<"请输入用户名"<<endl;
    cin>>user_name;
    cout<<"请输入密码"<<endl;
    string passwd;
    cin>>passwd;

    Json::Value val;
    val["type"] = Login;
    val["user_tel"] = user_tel;
    val["passwd"] = passwd;
    val["user_name"] = user_name;
    send(conn_fd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

   char buff_status[Length] = {0};
    int n = recv(conn_fd,buff_status,Length-1,0);
    if(n<0){
        cout<<"ser err"<<endl;
        return;
    }

    Json::Value Acmess;
    Json::Reader Read;
    if(!Read.parse(buff_status,Acmess)){
        cout<<"Json Read error!"<<endl;
        return;
    }

    string st = Acmess["status"].asString();
    if(st.compare(k)!=0){
        cout<<"Login false!"<<endl;
        return ;
    }

    cout<<"Login success!"<<endl;
    Dl_Status = true;
}

void Client::Show_Ticket(){
   Json::Value val;
   val["type"] = CheckApoint;
   send(conn_fd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

   char buff[Length]={0};
   int n = recv(conn_fd,buff,Length-1,0);
   if(n<=0){
    cout<<"ser close"<<endl;
    return;
   }

   Json::Value res_val;
   Json::Reader Read;
   if(!Read.parse(buff,res_val)){
     cout<<"Json 解析失败"<<endl;
     return;
   }

   string s = res_val["status"].asString();
   if(s.compare(k)!=0){
      cout<<"查看预约信息失败"<<endl;
      return;
   }

   int num = res_val["num"].asInt();
   if(num<0){
    cout<<"暂时没有可以预约的信息"<<endl;
    return ;
   }

   cout<<"|----------|--------------|--------|--------|------------|"<<endl;
   cout<<"|   序号   |     名称     | 总票数 | 已预定 |    时间    |"<<endl;
   cout<<"|----------|--------------|--------|--------|------------|"<<endl;

   for(int i=0;i<num;i++){
    printf("| %8s | %12s | %6s | %6s | %10s |\n",
           res_val["ticket_arr"][i]["ticket_id"].asString().c_str(),
           res_val["ticket_arr"][i]["ticket_name"].asString().c_str(),
           res_val["ticket_arr"][i]["ticket_max"].asString().c_str(),
           res_val["ticket_arr"][i]["ticket_count"].asString().c_str(),
           res_val["ticket_arr"][i]["day_time"].asString().c_str());
   }
   cout<<"|----------|--------------|--------|--------|------------|"<<endl;

}

void Client::Yd_Ticket(){
    Show_Ticket();
    cout<<"请输入需要预订的序号"<<endl;
    int index;
    cin>>index;
    Json::Value val;
    val["type"] = Appointment;
    val["user_tel"] = user_tel;
    val["ticket_id"] = to_string(index);

    send(conn_fd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

    char buff[Length] = {0};
    int n = recv(conn_fd,buff,Length-1,0);
    if(n<0){
        cout<<"ser close"<<endl;
        return ;
    }

    Json::Value res_val;
    Json::Reader Read;
    if(!Read.parse(buff,res_val)){
        cout<<"Json 解析失败"<<endl;
        return;
    }

    string s = res_val["status"].asString();
    if(s.compare(k)!=0){
        cout<<"预订失败"<<endl;
        return;
    }

    cout<<"预订成功！"<<endl;

}

void Client::Show_My_Ticket(){
  Json::Value val;
  val["type"] = AppoinMessage;
  val["user_tel"] = user_tel;
  send(conn_fd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

  char buff[Length]={0};
  int n = recv(conn_fd,buff,Length,0);
  if(n<=0){
    cout<<"ser close"<<endl;
    return;
  }

  Json::Value res_val;
  Json::Reader Read;
  if(!Read.parse(buff,res_val)){
      cout<<"Json 解析失败"<<endl;
     return;
  }

  string s = res_val["status"].asString();
  if(s.compare(k)!=0){
    cout<<"查看我的预约失败"<<endl;
    return;
  }

  int num = res_val["num"].asInt();
 if(num == 0){
    cout<<"您还没有预约任何信息"<<endl;
    return ;
}

   cout<<"|----------|--------------|--------|--------|------------|"<<endl;
   cout<<"|  预约ID  |     名称     | 总票数 | 已预定 |    时间    |"<<endl;
   cout<<"|----------|--------------|--------|--------|------------|"<<endl;

   for(int i=0;i<num;i++){
    printf("| %8s | %12s | %6s | %6s | %10s |\n",
           res_val["my_ticket_arr"][i]["yd_id"].asString().c_str(),
           res_val["my_ticket_arr"][i]["ticket_name"].asString().c_str(),
           res_val["my_ticket_arr"][i]["ticket_max"].asString().c_str(),
           res_val["my_ticket_arr"][i]["ticket_count"].asString().c_str(),
           res_val["my_ticket_arr"][i]["day_time"].asString().c_str());
   }
   cout<<"|----------|--------------|--------|--------|------------|"<<endl;
}

void Client::Qx_Ticket(){
    Show_My_Ticket();
    cout<<"请输入需要取消的预约ID:"<<endl;
    string yd_id;
    cin>>yd_id;

    Json::Value val;
    val["type"] = CancelAppoin;
    val["user_tel"] = user_tel;
   val["yd_id"] = yd_id;
   send(conn_fd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

   char buff[Length] = {0};
   int n = recv(conn_fd,buff,Length-1,0);
   if(n <= 0){
    cout<<"ser close"<<endl;
    return ;
   }

   Json::Value res_val;
   Json::Reader Read;
   if(!Read.parse(buff,res_val)){
    cout<<"Json解析失败"<<endl;
    return ;
   }

   string s = res_val["status"].asString();
   if(s.compare(k)!=0){
    cout<<"取消预约失败"<<endl;
    return ;
   }

   cout<<"取消预约成功"<<endl;
}

void Client::Run(){
    while(Running){
        Show_Menu();
        int tmp;
        cin>>tmp;
        if(tmp < Login || tmp > Exit){
            cout<<"输入错误"<<endl;
            continue;
        }
        if(Dl_Status){
            tmp = tmp+2;
        }
        else if(tmp==3){
            tmp = Exit;
        }
        switch(tmp){
            case Login:
                User_login();
                break;
            case Register:
                User_Register();
                break;
            case CheckApoint:
                Show_Ticket();
                break;
            case Appointment:
                Yd_Ticket();
                break;
            case AppoinMessage:
                Show_My_Ticket();
                break;
            case CancelAppoin:
                Qx_Ticket();
                break;
            case Exit:
                Running = false;
                break;
        }
    }
}


int main(){
    Client client;
    return 0;
}