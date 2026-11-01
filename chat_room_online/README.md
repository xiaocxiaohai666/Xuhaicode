# chat_room_online —— 多人在线聊天室

C++ / Linux 网络编程项目：**muduo 服务端 + POSIX socket 客户端**，支持账号、好友、群组、私聊/群聊与离线消息。

- 服务端：muduo `TcpServer` + `EventLoopThreadPool`，业务层单例 `ChatService`，MySQL 连接池持久化，Redis 发布订阅支撑跨进程投递
- 客户端：原生 socket + 双线程（主线程收发请求、独立接收线程处理服务端推送），请求/响应按 `seq` 配对
- 协议：JSON over TCP，自定义 `\n` 分隔符，两端共用 [include/public.hpp](include/public.hpp)

## 功能

| 分类 | 功能 |
|---|---|
| 账号 | 注册、登录、退出登录、注销账号、修改密码 |
| 好友 | 拉取好友列表、添加好友、删除好友 |
| 群组 | 创建群、加入群、退出群、修改群名/简介、转让群主、我的群列表、群成员 |
| 聊天 | 私聊、群聊；对方离线时消息入库，登录后补投 |

17 类请求，共 35 个消息类型（请求/响应成对出现，外加 1 个服务端主动下发的 `Chat_Push`）。

## 技术栈

| | 说明 |
|---|---|
| 语言 / 标准 | C++14（聚合初始化 + nlohmann/json 需要） |
| 网络 | muduo（服务端）/ POSIX socket（客户端） |
| 并发 | muduo IO 线程池；客户端 `mutex` + `condition_variable` |
| 存储 | MySQL（utf8mb4，支持中文与 emoji） |
| 跨进程 | Redis PUBLISH / SUBSCRIBE（按用户 id 建频道） |
| 协议 | JSON（nlohmann/json）+ `\n` 分隔符 |
| 构建 | CMake + [build.sh](build.sh) |

## 目录结构

```
chat_room_online/
├── CMakeLists.txt              # 顶层：C++14、输出到 bin/、查找 muduo
├── build.sh                    # 一键清空 build 并重新编译
├── bin/                        # 可执行文件输出（不入库）
├── docs/schema.sql             # 建库建表脚本
├── include/
│   ├── public.hpp              # 两端共用的协议定义（消息类型、返回码、业务结构体）
│   └── server/
│       ├── chatserver.hpp      # 网络层：拆包 + 连接生命周期
│       ├── chatservice.hpp     # 业务层：handler 注册表 + 在线用户表
│       ├── db/db.h             # MySQL 固定槽位连接池
│       ├── model/              # User / Group 模型
│       └── redis/redis.hpp     # Redis 客户端（pub / sub 双连接）
├── src/
│   ├── client/
│   │   ├── main.cpp            # 客户端全部逻辑
│   │   └── conf/client.conf
│   └── server/
│       ├── main.cpp            # 读配置 → 建连接池 → 起服务 → 信号退出
│       ├── chatserver.cpp
│       ├── chatservice.cpp
│       ├── db/db.cpp
│       ├── model/{user,group}.cpp
│       ├── redis/redis.cpp
│       └── conf/{server,db,redis}.conf
└── thirdparty/json.hpp         # nlohmann/json 单头文件
```

## 架构要点

**分层**：`ChatServer` 只做字节流拆包（`Buffer::findEOL` + `retrieveUntil`）和连接回调，所有业务都在 `ChatService`；连接断开后的状态清理（在线表、`users.status`、取消订阅）也归业务层。

**消息路由**：`ChatService` 用 `unordered_map<int, MsgHandler>` 注册 17 个 handler，避免 switch 分支爆炸；`online_`（`user_id → TcpConnectionPtr`）是本进程在线状态的唯一权威来源。

**本地优先 + Redis 兜底**：目标用户在本进程 `online_` 里 → 直接 `runInLoop` 发送；不在本进程 → `PUBLISH <user_id>`，返回订阅者数，**为 0** 说明确实没人订阅，才把消息写入 `offline_messages`，等对方登录后补投并删除。

**MySQL 连接池**：固定槽位数组 + `thread_local` 缓存槽位下标。muduo IO 线程数固定，线程与连接一对一，取连接 O(1)，不打锁、不轮询；池满直接失败，不阻塞等待。

**客户端配对**：每条请求带自增 `seq`，主线程置 `g_wait_seq` 后在条件变量上最多等 5 秒；接收线程只有序号匹配才唤醒它，服务端推送（`Chat_Push`）则直接打印，不参与配对。`send_all` 循环写满，`MSG_NOSIGNAL` 防止对端关闭时被 SIGPIPE 杀掉。

## 环境依赖

Ubuntu 20.04 实测（GCC 9.4）：

```bash
sudo apt install g++ cmake make libmysqlclient-dev libhiredis-dev mysql-server redis-server
```

muduo 需单独编译安装（默认装到 `/usr/local`，CMake 里按这个路径查找）：

```bash
git clone https://github.com/chenshuo/muduo.git
cd muduo && ./build.sh install
```

> 装到别处的话，改 [CMakeLists.txt](CMakeLists.txt) 里的 `find_path` / `find_library` 的 `PATHS`。

## 构建

```bash
./build.sh          # 清空 build 后完整编译，产物在 bin/
```

或手动：

```bash
mkdir -p build && cd build
cmake .. && make -j4
```

两个目标：`bin/chat_room_server`、`bin/chat_room_client`。

## 初始化数据库

```bash
mysql -uroot -p < docs/schema.sql
```

创建库 `chat_room_db`（utf8mb4）与 6 张表：`users`、`friends`、`groups`、`group_members`、`messages`、`offline_messages`。脚本可重复执行（均为 `CREATE TABLE IF NOT EXISTS`）。

> `groups` 是 MySQL 8 保留字，代码里所有 SQL 都用反引号 `` `groups` ``。

## 配置

四个文件都是 `key=value`，一行一条，`#` 开头为注释，等号两边不要空格、值不要引号。配置文件路径在编译时通过 CMake 注入（`SERVER_CONF_DIR` / `CLIENT_CONF_DIR`），运行时也可用 `argv[1]` 覆盖。

| 文件 | 键 | 说明 |
|---|---|---|
| [server.conf](src/server/conf/server.conf) | `ip` / `port` | 监听地址与端口（默认 `0.0.0.0:6000`） |
| | `threads` | IO 线程数（默认 4） |
| [db.conf](src/server/conf/db.conf) | `ip` / `port` / `name` / `user` / `passwd` | MySQL 连接信息 |
| | `charset` | 必须 `utf8mb4` |
| | `pool_size` | 连接池容量，**必须 ≥ `threads` + 1**（一个线程独占一条连接） |
| [redis.conf](src/server/conf/redis.conf) | `host` / `port` | Redis 地址 |
| | `connect_timeout_ms` | 建连超时，建议保持 200ms，网络异常时快速失败 |
| | `poll_ms` | 订阅线程唤醒粒度，越小登录后订阅空窗越短（默认 20） |
| | `retry_ms` | 断线重连间隔 |
| [client.conf](src/client/conf/client.conf) | `ip` / `port` | 要连接的服务器地址，需与 `server.conf` 一致 |

## 运行

服务端（先起）：

```bash
./bin/chat_room_server                 # 使用默认配置目录
./bin/chat_room_server /path/to/server.conf
```

客户端（每个终端一个）：

```bash
./bin/chat_room_client
./bin/chat_room_client /path/to/client.conf
```

启动后按菜单输入编号：`1` 注册 → `2` 登录 → 之后可加好友、建群、私聊/群聊。测两个用户就开两个客户端终端。

## 协议速览

```text
{"type":3,"seq":1,"code":0,"msg":"","data":{"name":"tom","passwd":"123"}}\n
```

| 字段 | 含义 |
|---|---|
| `type` | `MsgType`，请求/响应成对，`Chat_Push` 为服务端推送 |
| `seq` | 请求序号，服务端原样带回，客户端据此配对 |
| `code` | `StatusCode`，`Ok = 0`，失败时给出具体原因 |
| `msg` | 附加说明（失败原因） |
| `data` | 业务结构体序列化结果，字段定义见 [public.hpp](include/public.hpp) |

消息以 `\n` 结尾：服务端按行拆包，一条 `recv` 里出现多条会被逐条处理，半条则留在缓冲区等下次拼接。

## 已知限制

- 密码明文存储与传输，仅用于学习演示。
- 在线状态按进程维护，多实例部署时消息通过 Redis 转发；Redis 不可用则退化为单进程内投递。
- 客户端为命令行交互，未做消息历史查询接口（`messages` 表只落库不读取）。
