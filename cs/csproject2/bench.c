/* bench.c — 裸 TCP 长连接压测器 v2（对齐 csproject2 的 JSON over TCP 协议）
 *
 * 为什么不用 wrk：wrk 只发 HTTP 报文，本服务端是"裸 TCP 读一段 JSON"，
 * 收 HTTP 报文会 JSON 解析失败。
 *
 * 用法: ./bench <线程数> <每线程连接数> <每连接请求数> <login|check>
 *   总并发 = 线程数 × 每线程连接数
 *
 * v2 变更：失败按阶段分类（connect / send / recv），便于定位连接重置发生在哪一步。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SRV_IP "127.0.0.1"
#define SRV_PORT 8888
#define BUFSZ 65536

static int T, C, R;
static char *REQ;
static int REQLEN;

typedef struct {
    long ok;
    long conn_fail;   /* connect 失败 */
    long send_fail;   /* send 失败 */
    long recv_fail;   /* recv <= 0（对端重置/关闭） */
    long n;
    double *lat;
} tstat_t;

static int conn_server(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(SRV_PORT);
    a.sin_addr.s_addr = inet_addr(SRV_IP);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
    return fd;
}

static void *worker(void *arg) {
    tstat_t *st = (tstat_t *)arg;
    st->lat = (double *)malloc(sizeof(double) * (size_t)C * (size_t)R);
    st->ok = st->conn_fail = st->send_fail = st->recv_fail = st->n = 0;

    int *fds = (int *)malloc(sizeof(int) * (size_t)C);
    for (int i = 0; i < C; i++) fds[i] = conn_server();

    char *buf = (char *)malloc(BUFSZ);

    for (int i = 0; i < C; i++) {
        if (fds[i] < 0) { st->conn_fail++; continue; }
        for (int j = 0; j < R; j++) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            ssize_t w = send(fds[i], REQ, REQLEN, 0);
            if (w != REQLEN) { st->send_fail++; break; }
            ssize_t r = recv(fds[i], buf, BUFSZ, 0);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (r <= 0) { st->recv_fail++; break; }
            double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
                      + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
            st->lat[st->n++] = ms;
            st->ok++;
        }
        close(fds[i]);
    }

    free(fds);
    free(buf);
    return NULL;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "用法: %s <线程数> <每线程连接数> <每连接请求数> <login|check>\n", argv[0]);
        return 1;
    }
    T = atoi(argv[1]); C = atoi(argv[2]); R = atoi(argv[3]);
    const char *mode = argv[4];

    if (strcmp(mode, "login") == 0) {
        REQ = "{\"type\":1,\"user_tel\":\"13400000000\",\"user_name\":\""
              "\xe5\xb0\x8f\xe7\x8e\x8b\",\"passwd\":\"123456\"}";
    } else if (strcmp(mode, "check") == 0) {
        REQ = "{\"type\":3}";
    } else {
        fprintf(stderr, "未知模式: %s（支持 login / check）\n", mode);
        return 1;
    }
    REQLEN = (int)strlen(REQ);

    pthread_t *th = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)T);
    tstat_t *st = (tstat_t *)calloc((size_t)T, sizeof(tstat_t));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < T; i++) pthread_create(&th[i], NULL, worker, &st[i]);
    for (int i = 0; i < T; i++) pthread_join(th[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double sec = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;

    long ok = 0, cf = 0, sf = 0, rf = 0, n = 0;
    for (int i = 0; i < T; i++) {
        ok += st[i].ok; cf += st[i].conn_fail; sf += st[i].send_fail;
        rf += st[i].recv_fail; n += st[i].n;
    }

    double *all = (double *)malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    long k = 0;
    for (int i = 0; i < T; i++)
        for (long j = 0; j < st[i].n; j++) all[k++] = st[i].lat[j];
    qsort(all, (size_t)n, sizeof(double), cmp_double);

    double sum = 0;
    for (long i = 0; i < n; i++) sum += all[i];
    double avg = n ? sum / n : 0;
    long pi = (long)(n * 0.95); if (pi >= n) pi = n - 1;
    double p95 = n ? all[pi] : 0;

    printf("==== bench %s | 线程%d × 连接%d = 并发%d | 每连接%d 请求 ====\n",
           mode, T, C, T * C, R);
    printf("总耗时: %.3f s\n", sec);
    printf("成功请求: %ld  (连接丢失: connect=%ld send=%ld recv=%ld)\n", ok, cf, sf, rf);
    printf("QPS: %.1f\n", sec > 0 ? (double)ok / sec : 0.0);
    printf("平均延迟: %.2f ms | P95: %.2f ms | 最大: %.2f ms\n",
           avg, p95, n ? all[n - 1] : 0.0);

    for (int i = 0; i < T; i++) free(st[i].lat);
    free(all); free(th); free(st);
    return 0;
}
