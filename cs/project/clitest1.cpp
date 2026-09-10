#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main()
{
    int sockfd = -1;
    for (int i = 0; i < 100000; i++)
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            printf("socket create err\n");
            break;
        }

        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(6000);
        saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        int res = connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)); // 连接服务器，三次握手
        if (res == -1)
        {
            printf("connect err\n");
            break;
        }

        printf("connect success\n");
    }

    while (1)
    {
        char buff[128] = {0};
        printf("input:\n");
        fgets(buff, 128, stdin);
        if (strncmp(buff, "end", 3) == 0)
        {
            break;
        }

    }

    exit(0);
}