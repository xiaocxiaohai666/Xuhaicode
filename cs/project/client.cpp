#include "client.h"

enum OP_TYPE
{
    DL = 1,
    ZC,
    CKYY,
    YD,
    YDXX,
    QXYD,
    TC
};

bool Client::Connect()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        return false;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = inet_addr(ips.c_str());
    if (connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1)
    {
        return false;
    }

    return true;
}

void Client::Show_Menu()
{
    if (!dl_status)
    {
        cout << "---用户名:游客--------状态：未登陆---" << endl;
        cout << "1 登陆   2  注册   3  退出" << endl;
        cout << "---------------------------------" << endl;
        cout << "请输入选项编号:" << endl;
        cin >> User_Op;
        if (User_Op == 3)
        {
            User_Op = TC;
        }
    }
    else
    {
        cout << "---用户名：" << user_name << "--------状态：已登陆------" << endl;
        cout << "1 查看预约      2  预定 " << endl;
        cout << "3 查看我的预约   4 取消预定 " << endl;
        cout << " 5 退出" << endl;
        cout << "请输入选项编号:" << endl;
        cin >> User_Op;
        User_Op += 2;
    }
}

void Client::User_Register()
{

    cout << "请输入手机号码:" << endl;
    cin >> user_tel;
    cout << "请输入用户名:" << endl;
    cin >> user_name;
    cout << "请输入密码:" << endl;
    string passwd;
    cin >> passwd;

    Json::Value val;
    val["type"] = ZC;
    val["user_tel"] = user_tel;
    val["user_name"] = user_name;
    val["user_passwd"] = passwd;
    send(sockfd, val.toStyledString().c_str(), strlen(val.toStyledString().c_str()), 0);

    char buff_status[128] = {0};
    int n = recv(sockfd, buff_status, 127, 0);
    if (n <= 0)
    {
        cout << "ser err" << endl;
        return;
    }

    //因为Json信息的传输是依靠字符串流进行传输的，所以我们拿到时也是字符串流然后对其进行反序列化
    //最后得到Json格式的信息，方便我们进行获取。
    Json::Value resval;
    Json::Reader read;
    if (!read.parse(buff_status, resval))
    {
        cout << "Json 解析失败" << endl;
        return;
    }

    string st = resval["status"].asString();
    if (st.compare("OK") != 0)
    {
        cout << "注册失败" << endl;
        return;
    }

    cout << "注册成功" << endl;
    dl_status = true;
}
void Client::User_Login()
{
    cout<<"请输入手机号码"<<endl;
    cin>>user_tel;
    cout<<"请输入密码"<<endl;
    string passwd;
    cin>>passwd;
    
    Json::Value val;
    val["type"] = DL;
    val["user_tel"] = user_tel;
    val["user_passwd"] = passwd;

    send(sockfd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

    char status_buff[128] = {0};
    int n =recv(sockfd,status_buff,127,0);
    if(n<=0){
        cout<<"ser close"<<endl;
        return ;
    }
    
    Json::Value resval;
    Json::Reader Read;
    //Json::Reader::parse() 函数的作用就是：
//把一段字符串（status_buff）解析 → 转换成一个结构化的 Json::Value 对象（resval）
    if(!Read.parse(status_buff,resval)){
        cout<<"json  解析失败"<<endl;
        cout<<"登录失败"<<endl;
        return ;
    }

    string s = resval["status"].asString();
    if(s.compare("OK")!= 0){
        cout<<"登录失败"<<endl;
        return ;
    }

    user_name = resval["user_name"].asString();
    dl_status = true;

    cout<<"登录成功"<<endl;
 
}

void Client::Show_Ticket()
{
   Json::Value val;
   val["type"] = CKYY;
   send(sockfd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);
   
   char buff[1024] = {0};
   int n = recv(sockfd,buff,1023,0);
   if(n <= 0)
   {
    cout<<"ser close"<<endl;
    return ;
   }

   Json::Value res_val;
   Json::Reader Read;
   if(!Read.parse(buff,res_val)){
        cout<<"Json 解析错误"<<endl;
        return;
   }
   
   string s =res_val["status"].asString();
   if(s.compare("OK")!=0){
    cout<<"查看预约信息失败"<<endl;
    return;
   }

   int num = res_val["num"].asInt();
   if(num == 0){
    cout<<"暂时没有可以预约的信息"<<endl;
    return ;
   }

   cout<<"|    序号    |    名称    |  总票数   |  已预定  |  时间   |"<<endl;

   for(int i=0;i<num;i++){
    cout<<" "<<res_val["ticket_arr"][i]["ticket_id"].asString();
    cout<<" "<<res_val["ticket_arr"][i]["ticket_name"].asString();
    cout<<" "<<res_val["ticket_arr"][i]["ticket_max"].asString();
    cout<<" "<<res_val["ticket_arr"][i]["ticket_count"].asString();
    cout<<" "<<res_val["ticket_arr"][i]["day_time"].asString();
    cout<<endl;
   }


}
void Client::Yd_Ticket()//进行预定（预定）
{
   Show_Ticket();//先让它显示预约的信息
   cout<<"请输入需要预定的序号"<<endl;
   int index;
   cin>>index;
   Json::Value val;
   val["type"]= YD;
   val["user_tel"] = user_tel;
   val["ticket_id"] = to_string(index);
   //这里可以添加一个是否有不规范的数据
   send(sockfd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);
   
   char buff[128]= {0};
   int n = recv(sockfd,buff,127,0);
   if(n<=0){
    cout<<"ser close"<<endl;
    return ;
   }

   Json::Value res_val;
   Json::Reader Read;
   if(!Read.parse(buff,res_val)){
    cout<<"Json解析失败"<<endl;
    return;
   }

   string s = res_val["status"].asString();
   if(s.compare("OK")!=0){
    cout<<"预定失败"<<endl;
    return ;
   }

   cout<<"预订成功"<<endl;
   
}

void Client::Show_My_Ticket()
{
   Json::Value val;
   val["type"] = YDXX;
   val["user_tel"] = user_tel;
   send(sockfd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

   char buff[1024] = {0};
   int n = recv(sockfd,buff,1023,0);
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
   if(s.compare("OK")!=0){
    cout<<"查看我的预约失败"<<endl;
    return ;
   }

   int num = res_val["num"].asInt();
   if(num == 0){
    cout<<"您还没有预约任何信息"<<endl;
    return ;
   }

   cout<<"| 预约ID |  名称    | 总票数 | 已预定 |  时间   |"<<endl;
   for(int i=0;i<num;i++){
    cout<<" "<<res_val["my_ticket_arr"][i]["yd_id"].asString();
    cout<<" "<<res_val["my_ticket_arr"][i]["ticket_name"].asString();
    cout<<" "<<res_val["my_ticket_arr"][i]["ticket_max"].asString();
    cout<<" "<<res_val["my_ticket_arr"][i]["ticket_count"].asString();
    cout<<" "<<res_val["my_ticket_arr"][i]["day_time"].asString();
    cout<<endl;
   }
}

void Client::Qx_Ticket()
{
   cout<<"请输入需要取消的预约ID:"<<endl;
   string yd_id;
   cin>>yd_id;

   Json::Value val;
   val["type"] = QXYD;
   val["user_tel"] = user_tel;
   val["yd_id"] = yd_id;
   send(sockfd,val.toStyledString().c_str(),strlen(val.toStyledString().c_str()),0);

   char buff[128] = {0};
   int n = recv(sockfd,buff,127,0);
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
   if(s.compare("OK")!=0){
    cout<<"取消预约失败"<<endl;
    return ;
   }

   cout<<"取消预约成功"<<endl;
}

void Client::Run()
{
    while (runing)
    {
        Show_Menu();
        switch (User_Op)
        {
        case DL:
            User_Login();
            break;
        case ZC:
            User_Register();
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
            Qx_Ticket();
            break;
        case TC:
            runing = false;
            break;
        default:
            break;
        }
    }
}

int main()
{
    Client cli;
    if (!cli.Connect())
    {
        cout << "connect ser err" << endl;
        exit(1);
    }

    cli.Run();

    exit(0);
}