# 大王山票务预约系统

一个用 **C++** 写的 **C/S 架构**票务预约系统：客户端通过 **TCP 长连接**与服务端通信，服务端用 **epoll** 做 I/O 多路复用、**线程池**并发处理请求、**MySQL 连接池**复用数据库连接，协议用 **JSON**。

从最基础的一版做起，一共迭代了 **四个版本**，每一版都留下了完整的文档和踩坑记录。

## 业务功能

用户注册 · 用户登录 · 查看可预约票 · 预约票 · 查看我的预约 · 取消预约

预约与取消走 **MySQL 事务**，防止超卖、保证余票计数一致。

## 四个版本

| 目录 | 版本 | 关键变化 | 从这里开始读 |
|---|---|---|---|
| [`csproject/`](csproject/) | **v1** | 打地基：epoll + 线程池 + JSON 协议，数据库走**短连接** | [项目说明.md](csproject/项目说明.md) |
| [`csproject2/`](csproject2/) | **v2** | 把短连接换成 **MySQL 连接池**（10 条长连接复用），并补了 C 压测器与压测报告 | [预约系统版本2.md](csproject2/预约系统版本2.md) |
| [`csproject2view/`](csproject2view/) | **v2 + 可视化** | 加一层 **Flask 网页界面**，浏览器直接操作 | [预约系统版本2.md](csproject2view/预约系统版本2.md) |
| [`cs++view/`](cs++view/) | **v2 + C++11 化** | 把 pthread / 手动内存管理**彻底 C++11 化**（RAII） | [C++化改造记录.md](cs++view/C++化改造记录.md) |
| [`project/`](project/) | **需求与早期练习** | 完整[需求文档](project/需求文档.md) + 最早的单文件实现与练习 | [需求文档.md](project/需求文档.md) |

> 版本演进关系：`csproject` → `csproject2` → `csproject2view` → `cs++view`（最后一版是从 `csproject2view` 拷贝后做语法改造）。

## 技术栈

| 模块 | 技术 |
|---|---|
| 网络模型 | TCP + epoll（**LT 模式 + `EPOLLONESHOT`**） |
| 并发模型 | Reactor + pthread 线程池（**10 个 worker** + 条件变量唤醒） |
| 数据库 | MySQL C API（**连接池** + 事务） |
| 通信协议 | JSON over TCP（jsoncpp） |
| 配置管理 | 纯文本 `key=value`（`service.conf` / `mysql.conf`） |
| 构建 | Makefile |
| 可选 Web 层 | Python 3 + Flask |

## 架构

```
                  ┌──────────────────────────┐
                  │         Client           │
                  └────────────┬─────────────┘
                               │ TCP + JSON
                               ▼
      ┌─────────────────────────────────────────────────┐
      │                 Tcp_Service                     │
      │  ┌───────────┐    ┌──────────────────────┐      │
      │  │  epoll    │───▶│   Thread_Pool         │      │
      │  │(epoll_wait)│  │ (10 worker threads)   │      │
      │  └─────┬─────┘    └──────────┬───────────┘      │
      │   ┌────┴─────┐         ┌────┴────────┐          │
      │   │ listen_fd│         │  RecvSocket │          │
      │   │ (LT 模式)│         │  (处理业务)  │          │
      │   └──────────┘         └─────┬───────┘          │
      │                    ┌─────────▼─────────┐        │
      │                    │   Connect_Pool    │        │
      │                    │ 10 条 MySQL 长连接 │        │
      │                    └─────────┬─────────┘        │
      └──────────────────────────────┼──────────────────┘
                                     ▼
                            ┌────────────────┐
                            │     MySQL      │
                            └────────────────┘
```

带网页界面那版（`csproject2view/`）在最外层再加一层，外部用户只接触 5000 端口：

```
浏览器 ──HTTP(JSON)──▶ Flask (0.0.0.0:5000) ──TCP──▶ C++ service (127.0.0.1:9999) ──▶ MySQL
```

9999 端口**只监听本地**，不对外暴露。

## 核心设计

### 1. epoll：两种 fd，两种模式

| fd | 事件 | 为什么 |
|---|---|---|
| `listen_fd` | `LT` + `O_NONBLOCK` | 水平触发 + 非阻塞，`accept` 循环读到 `EAGAIN` 为止，主线程不会空转 |
| `conn_fd` | `EPOLLIN \| **EPOLLONESHOT**` | 处理期间 epoll 对这条 fd "失忆"，**杜绝多个 worker 同时操作同一个 fd**；处理完再 `epoll_ctl(MOD)` 重新武装 |

`epoll_event.data` 是 union，里面挂的是**连接对象的地址**（`data.ptr`），事件到达时直接拿到对应对象，不用查表。

### 2. 连接生命周期：清理顺序不能乱

```
epoll_ctl(EPOLL_CTL_DEL)  →  delete this  →  析构函数里 close(fd)
```

先摘掉事件、再析构对象、最后关 fd。顺序反了就会出现"幽灵事件"——fd 已经关了但 epoll 里还挂着，下一次 `epoll_wait` 返回一个已经失效的 fd。

### 3. MySQL 连接池：槽位 + 引用计数

服务端启动时 `Run()` **预建 10 条长连接**（账号密码读 `mysql.conf`）。每条连接是一个槽位：

```cpp
struct Mysql_cli {
    MYSQL* mysql_con;
    bool   status;      // 空闲 / 占用
};
```

- **借用**：线性扫描第一个空闲槽 → 标记占用，引用计数 `user++`
- **归还**：按连接指针 `mysql_con` 匹配到原槽位 → 置回空闲，`user--`
- **池满**：直接返回 `"MYSQL CLIENT BUSSY"`，客户端提示重试
- 另外封装了 `Begin()` / `Commit()` / `RollBack()`，预约与取消操作在其上跑事务

**为什么要有连接池**：MySQL 每次建立 TCP + 认证的开销远大于一次简单查询。v1 的短连接方案每个请求都要连一次，QPS 上不去，这是 v2 唯一的改造动机。

### 4. 通信协议

JSON over TCP，请求带 `cmd` 字段区分动作：

| cmd | 动作 |
|---:|---|
| 1 | Login |
| 2 | Register |
| 3 | CheckApoint（查可预约票） |
| 4 | Appointment（预约） |
| 5 | AppoinMessage（我的预约） |
| 6 | CancelAppoin（取消预约） |

## 已知短板与改进路线

诚实记录，来自 [预约系统版本2.md 第三章](csproject2view/预约系统版本2.md)：

| 优先级 | 问题 | 改进方向 |
|---|---|---|
| **最高** | **高并发写突发下的连接重置**——`listen_fd` 用 LT 且未挂 `EPOLLONESHOT`，主线程每轮都重复投递 `AccSocket` 任务；队列满后 `add_Task` 直接 `delete` 掉**仍有未读数据**的 `RecvSocket`，带着未读缓冲 `close` 会发 RST（读场景几乎不出现，写场景 50 并发 33 次） | 监听 fd 改 `EPOLLIN \| EPOLLONESHOT` + 处理后重挂；`add_Task` 溢出时不要 `delete` 活跃连接。详见 [csproject2/压测报告.md](csproject2/压测报告.md) 第五节「关键发现 6」 |
| **最高** | **连接池没有条件变量**——借不到连接直接 `return false`，第 11 个并发请求就被拒 | `Connect_Pool` 加 `pthread_cond_t`：借不到时 `cond_wait`，归还时 `cond_signal` |
| 高 | 10 个 worker 共享 10 条连接，借/还都要加锁 | worker 启动时绑死一条连接（`__thread` / `pthread_key`），**彻底无锁** |
| 中 | epoll 用 LT + ONESHOT，每次都要 `epoll_ctl(MOD)` 重新武装 | 改 ET + 非阻塞 fd，循环 `recv` 到 `EAGAIN`，减少系统调用 |
| 中 | 每次 `mysql_query` 拼 SQL 字符串，服务端每次都要重新 parse | 用 `mysql_stmt_prepare` + `mysql_stmt_bind_param` 预处理语句，复用执行计划 |
| 低 | 连接池没有探活（长时间空闲后连接可能已被 MySQL 断开） | 借用前 `mysql_ping` |
| 低 | 连接池析构用 `delete` 而非 `delete[]` 释放数组 | 改为 `delete[]` |

## 编译运行

### v1 / v2（命令行客户端）

```bash
cd csproject          # 或 csproject2 / cs++view
make clean && make all
./ser_start.sh        # 或 ./service service.conf
```

### v2 + 网页界面

```bash
cd csproject2view
./start_all.sh        # 自动：检查 MySQL → 启动 C++ 服务 → 装 Flask 依赖 → 启动 Web → 开浏览器
./stop_all.sh         # 按 PID 文件精确停止，不会误杀其他项目
```

浏览器访问 `http://localhost:5000`。

### 压测（v2）

`csproject2/` 下自带两个压测器，都直接说 TCP + JSON 协议（不能用 wrk，那玩意只发 HTTP 报文）：

```bash
cd csproject2
python3 stress_test.py login    50 20   # Python 多线程，快速跑业务场景
python3 stress_test.py check    50 20
python3 stress_test.py register 50 4
python3 stress_test.py appoint  50 1    # 并发抢同一张票，查超卖

gcc -O2 -pthread -o bench bench.c       # C 版长连接压测器，更接近真实 QPS
./bench 4 25 20 login                   # <线程数> <每线程连接数> <每连接请求数> <login|check>
```

实测结果、超卖验证与连接重置的根因分析见 [csproject2/压测报告.md](csproject2/压测报告.md)。

### 依赖（Ubuntu）

```bash
sudo apt install g++ make libjsoncpp-dev libmysqlclient-dev mysql-server
# 网页版另需
pip3 install flask
```

## 文档索引

| 文档 | 讲什么 |
|---|---|
| [project/需求文档.md](project/需求文档.md) | 完整需求：功能模块、数据库表设计、通信协议、类与函数声明详解、库依赖速查 |
| [csproject/项目说明.md](csproject/项目说明.md) | v1 架构说明 + epoll 关键设计 + 全量源码 |
| [csproject/项目所遇到的问题.md](csproject/项目所遇到的问题.md) | v1 踩坑记录：线程池临界区、"政府大厅"类比、`EPOLLONESHOT`、`data.ptr`、**短连接 vs 长连接 vs 连接池三种方案对比** |
| [csproject/epoll笔记.md](csproject/epoll笔记.md) | epoll 原理笔记：ADD/MOD/DEL 三个操作、ONESHOT 的"剪线"模型、`data` 是 union |
| [csproject2/预约系统版本2.md](csproject2/预约系统版本2.md) | v2 连接池设计与各文件详解 |
| [csproject2/压测报告.md](csproject2/压测报告.md) | v2 压测全过程：QPS/延迟数据、并发抢票超卖验证、**高并发写突发下连接重置的根因定位与改进方案**，以及 `-O0` / `-O2` 编译优化的同机对照复测（结论：优化基本不提速，瓶颈在 MySQL 往返与队列溢出，不在 CPU） |
| [csproject2view/预约系统版本2.md](csproject2view/预约系统版本2.md) | v2 + Flask 版：整体架构、各文件详解、改进建议与路线图 |
| [cs++view/C++化改造记录.md](cs++view/C++化改造记录.md) | 逐处列出从 C/pthread 到 C++11 的所有改动（带源码行号） |
| [cs++view/C++11线程与pthread对比.md](cs++view/C++11线程与pthread对比.md) | 两套线程 API 对照 + 9 类坑位总结 + 选型建议 |
| [cs++view/项目代码文档.md](cs++view/项目代码文档.md) | C++ 版全部代码文档 |
| [project/Makefile语法规则.md](project/Makefile语法规则.md) · [Shell脚本语法规则.md](project/Shell脚本语法规则.md) | 构建与启动脚本的两份速查手册 |

## CSDN 博客

图文版发在 [一只旭宝](https://blog.csdn.net/2501_90355051) 的专栏 [Linux环境下的票务预约系统](https://blog.csdn.net/2501_90355051/category_13202149.html)：

- [票务预约系统 - 需求文档](https://blog.csdn.net/2501_90355051/article/details/163889663)
- [票务预约系统（代码）](https://blog.csdn.net/2501_90355051/article/details/163994237)
- [票务预约系统项目所遇到的问题](https://blog.csdn.net/2501_90355051/article/details/163994508)
- [预约系统版本2（基于第一版改良）](https://blog.csdn.net/2501_90355051/article/details/164000487)
- [预约系统版本2（python+flask 可视化版本）](https://blog.csdn.net/2501_90355051/article/details/164016329)
- [预约系统项目（C++版）代码文档（cs++view）](https://blog.csdn.net/2501_90355051/article/details/164187149)
- [Makefile 语法规则速查手册](https://blog.csdn.net/2501_90355051/article/details/163885216)
- [Shell 脚本语法规则速查手册](https://blog.csdn.net/2501_90355051/article/details/163884832)
