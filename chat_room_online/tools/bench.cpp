// chat_room_online 压测器（纯 C，编译：gcc -O2 -D_GNU_SOURCE -pthread tools/bench.c -o tools/bench）
// 协议与 include/public.hpp 保持一致：JSON + '\n'，一条连接上一次只有一个未完成请求（同步收发）。
//
// 支持的场景：
//   ping      发一个服务端未注册的消息类型，只走“解析 + 查表 + 回错误包”，不碰数据库
//   register  并发注册用户，每次请求 1 次 INSERT
//   login     同一连接上 logout → login 循环，计时只算 login（SELECT + UPDATE）
//   chat      每线程先登录自己的用户，然后反复向一个在线目标用户发私聊（1 次 INSERT + 1 次推送）
//
// 用法示例：
//   ./tools/bench -m ping     -c 64 -n 2000
//   ./tools/bench -m register -c 32 -n 500
//   ./tools/bench -m login    -c 32 -n 500
//   ./tools/bench -m chat     -c 32 -n 500
// 其它参数：-h 服务器地址（默认 127.0.0.1） -p 端口（默认 6000） -k 批次标记（默认取时间戳）

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include "crypto.hpp"

// 与 public.hpp 的 MsgType 对应
enum {
    MSG_REGISTER_REQ = 1,
    MSG_LOGIN_REQ = 3,
    MSG_LOGOUT_REQ = 5,
    MSG_CHAT_PRIVATE_REQ = 31,
    MSG_CHAT_PUSH = 35
};

#define RBUF_MAX (64 * 1024)
#define LINE_MAX_LEN (16 * 1024)

typedef struct {
    int fd;
    long seq;
    char rbuf[RBUF_MAX];
    size_t rlen;
} conn_t;

typedef struct {
    int tid;
    int n;
    long tag;
} worker_arg;

static char g_ip[64] = "127.0.0.1";
static int g_port = 6000;
static long g_tag = 0;
static int g_nthreads = 16;
static int g_nper = 1000;
static int g_target_uid = 0;

static long long *g_lat = NULL;   // 每线程 n 个样本，失败请求写 -1
static size_t *g_ok = NULL;
static size_t *g_err = NULL;

static pthread_barrier_t g_barrier;
static conn_t g_target_conn;

// ---------------- 网络 ----------------

static int conn_open(void) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)g_port);
    if (inet_pton(AF_INET, g_ip, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

static void conn_init(conn_t *c, int fd) {
    c->fd = fd;
    c->seq = 0;
    c->rlen = 0;
}

static void conn_close(conn_t *c) {
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

// 取一帧：明文模式切 '\n'，加密模式按长度头切，两种都由 wire::decode 统一处理
static int recv_frame(conn_t *c, char *out, size_t outsz) {
    for (;;) {
        size_t consumed = 0;
        std::string plain;
        if (wire::decode(c->rbuf, c->rlen, consumed, plain)) {
            size_t cp = plain.size() < outsz - 1 ? plain.size() : outsz - 1;
            memcpy(out, plain.data(), cp);
            out[cp] = '\0';
            memmove(c->rbuf, c->rbuf + consumed, c->rlen - consumed);
            c->rlen -= consumed;
            return 0;
        }
        if (c->rlen >= RBUF_MAX) {
            return -1;
        }
        const ssize_t r = recv(c->fd, c->rbuf + c->rlen, RBUF_MAX - c->rlen, 0);
        if (r <= 0) {
            return -1;
        }
        c->rlen += (size_t)r;
    }
}

// 在 JSON 里找 "key":123 这样的整数，键名带引号，避免和字符串内容里的同名文本混淆
static int find_int(const char *s, const char *key, long long *out) {
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(s, pat);
    if (p == NULL) {
        return -1;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    *out = strtoll(p, NULL, 10);
    return 0;
}

// 发一条请求并等待序号匹配的响应；服务端推送（Chat_Push）直接丢弃
static int bench_request(conn_t *c, int type, const char *data, int *out_code,
                         char *line_out, size_t line_out_sz) {
    const int seq = ++c->seq;
    char msg[4608];
    snprintf(msg, sizeof msg,
             "{\"type\":%d,\"seq\":%d,\"code\":0,\"msg\":\"\",\"data\":%s}",
             type, seq, data);
    const std::string frame = wire::encode(std::string(msg));
    if (frame.empty() || send_all(c->fd, frame.data(), frame.size()) < 0) {
        return -1;
    }

    char line[LINE_MAX_LEN];
    for (;;) {
        if (recv_frame(c, line, sizeof line) < 0) {
            return -1;
        }
        long long t = 0, s = 0, code = 0;
        if (find_int(line, "type", &t) < 0 || find_int(line, "seq", &s) < 0) {
            continue;
        }
        if (t == MSG_CHAT_PUSH) {
            continue;
        }
        if (s != seq) {
            continue;
        }
        find_int(line, "code", &code);
        if (out_code != NULL) {
            *out_code = (int)code;
        }
        if (line_out != NULL) {
            snprintf(line_out, line_out_sz, "%s", line);
        }
        return 0;
    }
}

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// ---------------- 业务封装 ----------------

static int do_register(conn_t *c, const char *name, const char *passwd) {
    char body[256];
    snprintf(body, sizeof body, "{\"name\":\"%s\",\"passwd\":\"%s\"}", name, passwd);
    int code = 0;
    if (bench_request(c, MSG_REGISTER_REQ, body, &code, NULL, 0) < 0) {
        return -1;
    }
    return code;
}

static int do_login(conn_t *c, const char *name, const char *passwd, int *uid) {
    char body[256];
    snprintf(body, sizeof body, "{\"name\":\"%s\",\"passwd\":\"%s\"}", name, passwd);
    char line[LINE_MAX_LEN];
    int code = 0;
    if (bench_request(c, MSG_LOGIN_REQ, body, &code, line, sizeof line) < 0) {
        return -1;
    }
    if (code == 0 && uid != NULL) {
        long long v = 0;
        if (find_int(line, "user_id", &v) == 0) {
            *uid = (int)v;
        }
    }
    return code;
}

static int do_logout(conn_t *c, int uid) {
    char body[128];
    snprintf(body, sizeof body, "{\"user_id\":%d}", uid);
    int code = 0;
    return bench_request(c, MSG_LOGOUT_REQ, body, &code, NULL, 0) < 0 ? -1 : code;
}

static int do_chat(conn_t *c, int from, int to, const char *content) {
    char body[512];
    snprintf(body, sizeof body,
             "{\"from_id\":%d,\"to_id\":%d,\"content\":\"%s\",\"timestamp\":0}",
             from, to, content);
    int code = 0;
    return bench_request(c, MSG_CHAT_PRIVATE_REQ, body, &code, NULL, 0) < 0 ? -1 : code;
}

// 目标用户所在连接只负责把收到的推送读走，否则服务端发送缓冲区会堆满
static void *target_drain(void *arg) {
    conn_t *c = (conn_t *)arg;
    char line[LINE_MAX_LEN];
    while (recv_frame(c, line, sizeof line) == 0) {
    }
    return NULL;
}

// ---------------- 各场景 ----------------

static void *worker_ping(void *arg) {
    worker_arg *a = (worker_arg *)arg;
    long long *lat = g_lat + (size_t)a->tid * a->n;
    const int fd = conn_open();
    if (fd < 0) {
        g_err[a->tid] = (size_t)a->n;
        pthread_barrier_wait(&g_barrier);
        return NULL;
    }
    conn_t c;
    conn_init(&c, fd);
    pthread_barrier_wait(&g_barrier);

    for (int i = 0; i < a->n; ++i) {
        const long long t0 = now_ns();
        int code = 0;
        if (bench_request(&c, 9999, "{}", &code, NULL, 0) < 0) {
            lat[i] = -1;
            ++g_err[a->tid];
            continue;
        }
        lat[i] = now_ns() - t0;
        ++g_ok[a->tid];
    }
    conn_close(&c);
    return NULL;
}

static void *worker_register(void *arg) {
    worker_arg *a = (worker_arg *)arg;
    long long *lat = g_lat + (size_t)a->tid * a->n;
    const int fd = conn_open();
    if (fd < 0) {
        g_err[a->tid] = (size_t)a->n;
        pthread_barrier_wait(&g_barrier);
        return NULL;
    }
    conn_t c;
    conn_init(&c, fd);
    pthread_barrier_wait(&g_barrier);

    for (int i = 0; i < a->n; ++i) {
        char name[64];
        snprintf(name, sizeof name, "b%ld_%d_%d", a->tag, a->tid, i);
        const long long t0 = now_ns();
        const int code = do_register(&c, name, "p");
        const long long dt = now_ns() - t0;
        if (code != 0) {
            lat[i] = -1;
            ++g_err[a->tid];
            continue;
        }
        lat[i] = dt;
        ++g_ok[a->tid];
    }
    conn_close(&c);
    return NULL;
}

static void *worker_login(void *arg) {
    worker_arg *a = (worker_arg *)arg;
    long long *lat = g_lat + (size_t)a->tid * a->n;
    const int fd = conn_open();
    if (fd < 0) {
        g_err[a->tid] = (size_t)a->n;
        pthread_barrier_wait(&g_barrier);
        return NULL;
    }
    conn_t c;
    conn_init(&c, fd);

    char name[64];
    snprintf(name, sizeof name, "b%ld_%d", a->tag, a->tid);
    if (do_register(&c, name, "p") < 0) {
        g_err[a->tid] = (size_t)a->n;
        pthread_barrier_wait(&g_barrier);
        conn_close(&c);
        return NULL;
    }
    int uid = 0;
    if (do_login(&c, name, "p", &uid) != 0) {
        g_err[a->tid] = (size_t)a->n;
        pthread_barrier_wait(&g_barrier);
        conn_close(&c);
        return NULL;
    }
    pthread_barrier_wait(&g_barrier);

    for (int i = 0; i < a->n; ++i) {
        do_logout(&c, uid);            // 不计时，只是为了能再次登录
        const long long t0 = now_ns();
        int code = do_login(&c, name, "p", &uid);
        const long long dt = now_ns() - t0;
        if (code != 0) {
            lat[i] = -1;
            ++g_err[a->tid];
            continue;
        }
        lat[i] = dt;
        ++g_ok[a->tid];
    }
    conn_close(&c);
    return NULL;
}

static void *worker_chat(void *arg) {
    worker_arg *a = (worker_arg *)arg;
    long long *lat = g_lat + (size_t)a->tid * a->n;
    const int fd = conn_open();
    if (fd < 0) {
        g_err[a->tid] = (size_t)a->n;
        pthread_barrier_wait(&g_barrier);
        return NULL;
    }
    conn_t c;
    conn_init(&c, fd);

    char name[64];
    snprintf(name, sizeof name, "b%ld_%d", a->tag, a->tid);
    do_register(&c, name, "p");
    int uid = 0;
    if (do_login(&c, name, "p", &uid) != 0 || uid <= 0) {
        g_err[a->tid] = (size_t)a->n;
        pthread_barrier_wait(&g_barrier);
        conn_close(&c);
        return NULL;
    }
    pthread_barrier_wait(&g_barrier);

    for (int i = 0; i < a->n; ++i) {
        char content[128];
        snprintf(content, sizeof content, "benchmsg_%ld_%d_%d", a->tag, a->tid, i);
        const long long t0 = now_ns();
        const int code = do_chat(&c, uid, g_target_uid, content);
        const long long dt = now_ns() - t0;
        if (code != 0) {
            lat[i] = -1;
            ++g_err[a->tid];
            continue;
        }
        lat[i] = dt;
        ++g_ok[a->tid];
    }
    conn_close(&c);
    return NULL;
}

// ---------------- 统计 ----------------

static int cmp_ll(const void *a, const void *b) {
    const long long x = *(const long long *)a;
    const long long y = *(const long long *)b;
    return (x > y) - (x < y);
}

static void print_stats(const char *mode, const double elapsed) {
    const size_t cap = (size_t)g_nthreads * g_nper;
    long long *v = (long long *)malloc(cap * sizeof(long long));
    size_t m = 0, ok = 0, err = 0;
    for (int t = 0; t < g_nthreads; ++t) {
        ok += g_ok[t];
        err += g_err[t];
        for (int i = 0; i < g_nper; ++i) {
            const long long x = g_lat[(size_t)t * g_nper + i];
            if (x >= 0) {
                v[m++] = x;
            }
        }
    }
    if (m > 0) {
        qsort(v, m, sizeof(long long), cmp_ll);
    }

    const double qps = elapsed > 0 ? (double)ok / elapsed : 0.0;
    const double ms = 1000000.0;
    double avg = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
    if (m > 0) {
        double sum = 0;
        for (size_t i = 0; i < m; ++i) {
            sum += (double)v[i];
        }
        avg = sum / (double)m;
        p50 = (double)v[(size_t)(0.50 * (double)(m - 1))] ;
        p95 = (double)v[(size_t)(0.95 * (double)(m - 1))];
        p99 = (double)v[(size_t)(0.99 * (double)(m - 1))];
        max = (double)v[m - 1];
    }

    printf("mode=%-8s c=%-4d n=%-5d ok=%-8zu err=%-6zu elapsed=%.3fs "
           "qps=%8.1f avg=%.3fms p50=%.3fms p95=%.3fms p99=%.3fms max=%.3fms\n",
           mode, g_nthreads, g_nper, ok, err, elapsed, qps,
           avg / ms, p50 / ms, p95 / ms, p99 / ms, max / ms);
    free(v);
}

// ---------------- main ----------------

int main(int argc, char **argv) {
    if (!wire::init()) {
        fprintf(stderr, "加密模块初始化失败\n");
        return 1;
    }

    const char *mode = "ping";
    int opt;
    while ((opt = getopt(argc, argv, "m:c:n:h:p:k:")) != -1) {
        switch (opt) {
        case 'm': mode = optarg; break;
        case 'c': g_nthreads = atoi(optarg); break;
        case 'n': g_nper = atoi(optarg); break;
        case 'h': snprintf(g_ip, sizeof g_ip, "%s", optarg); break;
        case 'p': g_port = atoi(optarg); break;
        case 'k': g_tag = atol(optarg); break;
        default:
            fprintf(stderr, "用法: %s -m ping|register|login|chat [-c 并发] [-n 每线程请求数] "
                            "[-h ip] [-p port] [-k 批次标记]\n", argv[0]);
            return 1;
        }
    }
    if (g_nthreads <= 0 || g_nper <= 0) {
        fprintf(stderr, "并发和每线程请求数必须为正整数\n");
        return 1;
    }
    if (g_tag == 0) {
        g_tag = (long)time(NULL) % 1000000;
    }

    g_lat = (long long *)calloc((size_t)g_nthreads * g_nper, sizeof(long long));
    g_ok = (size_t *)calloc((size_t)g_nthreads, sizeof(size_t));
    g_err = (size_t *)calloc((size_t)g_nthreads, sizeof(size_t));
    if (g_lat == NULL || g_ok == NULL || g_err == NULL) {
        fprintf(stderr, "内存分配失败\n");
        return 1;
    }

    pthread_barrier_init(&g_barrier, NULL, (unsigned)g_nthreads + 1);
    void *(*fn)(void *) = worker_ping;
    if (strcmp(mode, "register") == 0) {
        fn = worker_register;
    } else if (strcmp(mode, "login") == 0) {
        fn = worker_login;
    } else if (strcmp(mode, "chat") == 0) {
        fn = worker_chat;
    }

    pthread_t tgt_thread;
    int has_target = 0;

    // chat 场景：先准备一个固定目标用户并保持在线，消息都打给他
    if (strcmp(mode, "chat") == 0) {
        const int fd = conn_open();
        if (fd < 0) {
            fprintf(stderr, "无法连接 %s:%d\n", g_ip, g_port);
            return 1;
        }
        conn_init(&g_target_conn, fd);
        char tname[64];
        snprintf(tname, sizeof tname, "b%ld_target", g_tag);
        do_register(&g_target_conn, tname, "p");
        if (do_login(&g_target_conn, tname, "p", &g_target_uid) != 0 || g_target_uid <= 0) {
            fprintf(stderr, "目标用户登录失败，请确认服务端已启动\n");
            return 1;
        }
        pthread_create(&tgt_thread, NULL, target_drain, &g_target_conn);
        has_target = 1;
    }

    worker_arg *args = (worker_arg *)calloc((size_t)g_nthreads, sizeof(worker_arg));
    pthread_t *tids = (pthread_t *)calloc((size_t)g_nthreads, sizeof(pthread_t));
    if (args == NULL || tids == NULL) {
        fprintf(stderr, "内存分配失败\n");
        return 1;
    }

    for (int i = 0; i < g_nthreads; ++i) {
        args[i].tid = i;
        args[i].n = g_nper;
        args[i].tag = g_tag;
        pthread_create(&tids[i], NULL, fn, &args[i]);
    }

    // 主线程也进栅栏：所有 worker 的建连/准备工作都完成后再统一起跑
    pthread_barrier_wait(&g_barrier);
    const long long t0 = now_ns();
    for (int i = 0; i < g_nthreads; ++i) {
        pthread_join(tids[i], NULL);
    }
    const long long t1 = now_ns();
    const double elapsed = (double)(t1 - t0) / 1e9;

    if (has_target) {
        // shutdown 打断阻塞的 recv，让目标线程退出
        shutdown(g_target_conn.fd, SHUT_RDWR);
        pthread_join(tgt_thread, NULL);
        conn_close(&g_target_conn);
    }

    print_stats(mode, elapsed);

    free(args);
    free(tids);
    free(g_lat);
    free(g_ok);
    free(g_err);
    pthread_barrier_destroy(&g_barrier);
    return 0;
}
