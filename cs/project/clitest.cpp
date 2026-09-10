#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int main()
{
    #if 0
    int sockfd=-1;
    for(int i=0;i<1000;i++)
    {
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("socket error\n");
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(6000);
    saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)); // 连接服务器，三次握手。
    if (res == -1)
    {
        perror("connect error\n");
        close(sockfd);
    
    }
    }
    #endif

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("socket error\n");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(6000);
    saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)); // 连接服务器，三次握手。
    if (res == -1)
    {
        perror("connect error\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    while (1)
    {
        char buff[128] = {0};
        printf("input:");
        fgets(buff, 128, stdin);
        if(strncmp(buff,"end",3)==0)
        {
            break;
        }
        #if 1
        send(sockfd, buff, strlen(buff) - 1, 0); // 发送数据
        memset(buff, 0, sizeof(buff));
        recv(sockfd, buff, strlen(buff) - 1, 0); // 接收数据
        printf("buff=%s\n", buff);
        #endif
    }
    close(sockfd);
    exit(0);
}//netstat -natp