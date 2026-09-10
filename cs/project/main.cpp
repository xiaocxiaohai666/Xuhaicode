#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <sys/epoll.h>
#include<jsoncpp/json/json.h>
using namespace std;



int main()
{
Json::Value val;
val["name"]="xiaoc";   
val["age"]=18;
val["sex"]="man";

cout<<val.toStyledString()<<endl;
Json::Value val2;
Json::Reader r;
r.parse(val.toStyledString(),val2);
cout<<val2["sex"].asString()<<endl;
return 0;
}