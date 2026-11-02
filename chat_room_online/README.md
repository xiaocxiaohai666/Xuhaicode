# chat_room_online —— 多人在线聊天室

C++ / Linux 网络编程项目：**muduo 服务端 + POSIX socket 客户端**，支持账号、好友、群组、私聊/群聊与离线消息。

- 服务端：muduo `TcpServer` + `EventLoopThreadPool`，业务层单例 `ChatService`，MySQL 连接池持久化，Redis 发布订阅支撑跨进程投递
- 客户端：原生 socket + 双线程（主线程收发请求、独立接收线程处理服务端推送），请求/响应按 `seq` 配对
- 协议：JSON over TCP，默认走 **AES-256-GCM 加密帧**，可编译期关闭退回明文 `\n` 分隔，两端共用 [include/public.hpp](include/public.hpp)

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
| 协议 | JSON（nlohmann/json），帧格式见「加密传输」 |
| 加密 | AES-256-GCM（OpenSSL `libcrypto`），默认开启 |
| 构建 | CMake + [build.sh](build.sh) |

## 目录结构

```
chat_room_online/
├── CMakeLists.txt              # 顶层：C++14、默认 RelWithDebInfo、ENABLE_CRYPTO 开关
├── build.sh                    # 一键清空 build 并重新编译
├── bin/                        # 可执行文件输出（不入库）
├── docs/schema.sql             # 建库建表脚本
├── include/
│   ├── public.hpp              # 两端共用的协议定义（消息类型、返回码、业务结构体）
│   ├── crypto.hpp              # 统一收发编解码（明文 / AES-256-GCM 两种模式）
│   └── server/
│       ├── chatserver.hpp      # 网络层：拆包 + 连接生命周期
│       ├── chatservice.hpp     # 业务层：handler 注册表 + 在线用户表
│       ├── db/db.h             # MySQL 通用连接池
│       ├── model/              # User / Group 模型
│       └── redis/redis.hpp     # Redis 客户端（pub / sub 双连接）
├── src/
│   ├── crypto.cpp              # 编解码实现
│   ├── client/
│   │   ├── main.cpp            # 客户端全部逻辑
│   │   └── conf/client.conf
│   └── server/
│       ├── main.cpp            # 读配置 → 初始化编解码 → 建连接池 → 起服务 → 信号退出
│       ├── chatserver.cpp
│       ├── chatservice.cpp
│       ├── db/db.cpp
│       ├── model/{user,group}.cpp
│       ├── redis/redis.cpp
│       └── conf/{server,db,redis}.conf
├── tools/bench.cpp             # 分场景压测器（复用 crypto 模块，可压加密/明文）
└── thirdparty/json.hpp         # nlohmann/json 单头文件
```

## 架构要点

**分层**：`ChatServer` 只做字节流拆包和连接回调，所有业务都在 `ChatService`；连接断开后的状态清理（在线表、`users.status`、取消订阅）也归业务层。

**消息路由**：`ChatService` 用 `unordered_map<int, MsgHandler>` 注册 17 个 handler，避免 switch 分支爆炸；`online_`（`user_id → TcpConnectionPtr`）是本进程在线状态的唯一权威来源。

**本地优先 + Redis 兜底**：目标用户在本进程 `online_` 里 → 直接 `runInLoop` 发送；不在本进程 → `PUBLISH <user_id>`，返回订阅者数，**为 0** 说明确实没人订阅，才把消息写入 `offline_messages`，等对方登录后补投并删除。

**MySQL 连接池**：`pool_size` 条连接常驻，`mutex` 保护的**空闲下标队列**做取用/归还，取和还都是 O(1)，不做线性扫描。

- 取连接返回 `Guard` 对象，析构即归还（RAII），业务代码不会漏还；
- 池满时 `condition_variable` 最多等 3 秒，超时返回失败，避免无限阻塞 IO 线程；
- 取用前 `mysql_ping` 探活，被服务端 `wait_timeout` 掐断的连接当场重建；
- 读操作遇到连接错误会重连并重试一次（幂等），**写操作只重连不重试**，避免重复插入。

线程数与池容量不再强绑定：`pool_size` 只需覆盖峰值并发，不必等于 `threads + 1`。

**客户端配对**：每条请求带自增 `seq`，主线程置 `g_wait_seq` 后在条件变量上最多等 5 秒；接收线程只有序号匹配才唤醒它，服务端推送（`Chat_Push`）则直接打印，不参与配对。`send_all` 循环写满，`MSG_NOSIGNAL` 防止对端关闭时被 SIGPIPE 杀掉。

## 加密传输

收发统一走 `wire` 命名空间（[include/crypto.hpp](include/crypto.hpp)），业务代码只调用 `wire::encode()` / `wire::decode()`，是否加密由编译期开关决定，两条路径共用同一套调用点。

加密帧格式：

```
+--------+--------+------------------+--------+
| len(4) | IV(12) | ciphertext(n)    | tag(16)|
+--------+--------+------------------+--------+
```

- `len` 为大端 32 位，表示 `IV + 密文 + tag` 的总长度；
- 每条消息随机生成 12 字节 IV，**绝不复用**；
- `tag` 是 GCM 认证标签，校验失败即丢弃该帧，同时具备防窃听与防篡改；
- 关闭加密时退回原来的 `JSON + '\n'` 行文本协议。

AES-256-GCM 由 OpenSSL 提供，密钥默认内置在 [src/crypto.cpp](src/crypto.cpp)（学习演示用）。要换成外部密钥，在 `wire::init()` 时传入 64 位十六进制字符串即可：

```cpp
wire::init("00112233...ff");   // 64 个 hex 字符 = 32 字节
```

**两端必须使用同一个开关和同一把密钥**，否则无法互通。

> 内置密钥只能防链路抓包（嗅探到的是密文），挡不住拿到二进制后的逆向提取。生产环境应改为从配置或环境变量注入。

实测开销（8 核虚拟机，同一环境 A/B/A 交替三轮）：

| 场景 | 明文 | 加密 | 开销 |
|---|---|---|---|
| ping（纯框架层） | 20,971 | 15,501 | −26% |
| register | 1,770 | 1,550 | −12% |
| chat | 1,590 | 1,506 | −5% |
| login | 577 | 606 | 噪声内 |

请求越轻，加密的相对开销越大；DB 场景的单次往返占大头，加密被稀释到 5% 以内。

## 环境依赖

Ubuntu 20.04 实测（GCC 9.4）：

```bash
sudo apt install g++ cmake make libmysqlclient-dev libhiredis-dev libssl-dev mysql-server redis-server
```

muduo 需单独编译安装（默认装到 `/usr/local`，CMake 里按这个路径查找）：

```bash
git clone https://github.com/chenshuo/muduo.git
cd muduo && ./build.sh install
```

> 装到别处的话，改 [CMakeLists.txt](CMakeLists.txt) 里的 `find_path` / `find_library` 的 `PATHS`。

## 构建

```bash
./build.sh                  # 默认开启加密
./build.sh --no-crypto      # 关闭加密，退回明文行协议
```

或手动：

```bash
mkdir -p build && cd build
cmake -DENABLE_CRYPTO=ON .. && make -j4
```

三个目标：`bin/chat_room_server`、`bin/chat_room_client`、`bin/bench`。

`ENABLE_CRYPTO` 默认 `ON`。关闭后不需要链接 OpenSSL，适合只想看协议本身的场景。构建类型默认 `RelWithDebInfo`（`-O2 -g`）；**不要留空 `CMAKE_BUILD_TYPE`**，否则整体变成 `-O0`，吞吐会掉一个量级。

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
| | `threads` | IO 线程数（默认 16，需按 CPU 核数调整） |
| [db.conf](src/server/conf/db.conf) | `ip` / `port` / `name` / `user` / `passwd` | MySQL 连接信息 |
| | `charset` | 必须 `utf8mb4` |
| | `pool_size` | 连接池容量，建议 ≥ `threads` + 4，且不超过 `max_connections / 实例数` |
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

## 压测

[tools/bench.cpp](tools/bench.cpp) 是自带的分场景压测器，四个场景互相隔离，能把「框架开销」和「落库开销」分开看：

```bash
./bin/bench -m ping     -c 128 -n 2000 -k 1    # 只建连 + 发请求，不碰数据库
./bin/bench -m register -c 32  -n 300  -k 2    # 注册：单次 INSERT
./bin/bench -m login    -c 32  -n 300  -k 3    # 登录：4 次 DB 往返 + Redis 订阅
./bin/bench -m chat     -c 32  -n 500  -k 4    # 私聊：INSERT + 在线推送
```

| 参数 | 含义 |
|---|---|
| `-m` | 场景：`ping` / `register` / `login` / `chat` |
| `-c` | 并发连接数（线程数） |
| `-n` | 每个并发连接发送的请求数 |
| `-k` | 用户名生成种子，**每次必须换新值** |

`-k` 决定注册时生成的用户名（`b<tag>_<线程号>_<序号>`）。**重复使用同一个 tag 会撞上已注册的用户名，重名会被计为错误**，看起来像"整线程失败"，其实是压测器自身的设计。跑多轮请递增 tag。

输出包含 QPS、p50/p90/p99 延迟、成功/失败数。压测器与服务端跑在同一台机器上会互相抢 CPU，实测值偏保守；要拿准确数字请把压测机分开部署。

## 协议速览

明文模式下的报文：

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

明文模式下消息以 `\n` 结尾：服务端按行拆包，一条 `recv` 里出现多条会被逐条处理，半条则留在缓冲区等下次拼接。加密模式下改为长度前缀 + 密文帧，拆包逻辑相应换成按 `len` 取帧。

## 已知限制

- 密码明文存储（仅用于学习演示），传输过程已加密。
- 加密密钥内置在源码中，只能防链路嗅探，不能抵抗拿到二进制后的逆向。
- 两端加密开关必须一致，没有版本协商机制。
- 在线状态按进程维护，多实例部署时消息通过 Redis 转发；Redis 不可用则退化为单进程内投递。
- 客户端为命令行交互，未做消息历史查询接口（`messages` 表只落库不读取）。
- 数据库写入仍是每条语句一次事务。实测单表写入在 3 核左右饱和，进一步提升需要改为批量落库 + 分表。
