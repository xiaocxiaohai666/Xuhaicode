# epoll 学习笔记

## 核心比喻:卡片 + 线 + 门

epoll 内部就是一堆卡片,每个被注册的 fd 对应一张卡片。

### 卡片长这样

```
┌────────────────────────────┐
│ data: 你存的东西(ptr/fd) │  ← 你 ADD 时存的内容
│ 线 → (连到对应 socket 的门)│  ← 一根线
└────────────────────────────┘
```

### ADD 一个 fd 时,epoll 做 3 件事

1. 做一张卡片,把你给的 data 写上去
2. 卡片旁边绑一根线
3. 线的另一头系到这个 fd 对应的 socket 的门上

### 客户端发消息时

```
socket 收到数据 → socket 的门自己开了
       ↓
门一开 → 扯动了绑在门上的那根线
       ↓
线一动 → 对应的卡片被拎出来
       ↓
卡片被丢进【就绪堆】
       ↓
epoll_wait 来就绪堆取卡片,看到卡片上写的 data
       ↓
把 data 还给你(你的对象指针或 fd)
```

## 关键原理

- **不是 epoll 主动去找哪个 fd 有数据**(那叫轮询,效率低)
- **是 socket 有数据时,门自己开,扯动线,拎出对应卡片**(这叫回调驱动,几乎零 CPU)
- **卡片和 socket 的对应关系 = 那根线**(ADD 时绑好,后续不用查表)
- **epoll_wait 不关心是谁触发的**,它只去就绪堆里取卡片,有什么取什么

## epoll_ctl 三个操作对比

| 操作 | 作用 | 比喻 |
|---|---|---|
| `EPOLL_CTL_ADD` | 把 fd 加进 epoll,首次注册 | 做新卡片 + 绑线 |
| `EPOLL_CTL_MOD` | 修改已注册 fd 的事件设置 | 修改卡片 + 重新绑线 |
| `EPOLL_CTL_DEL` | 把 fd 从 epoll 删除 | 撕掉卡片 + 剪线 |

## epoll_ctl 4 参数完整对照

```c
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
```

| 参数 | 作用 | 比喻 |
|---|---|---|
| `epfd` | 哪个 epoll 实例(可以有多个 epoll,卡片要挂到具体某个的红黑树) | 卡片存放在哪个 epoll |
| `op` | 操作类型:ADD / MOD / DEL | 做什么操作(存 / 改 / 删) |
| `fd` | 要监听哪个 fd(内核通过它找 socket 的门) | 哪扇门 |
| `event` | events = 关心什么事件;data = 触发后返回什么 | 何时触发 + 触发返回什么 |

### 关于 events 的精确表述

events 表示"关心什么事件类型",不是"何时触发":
- `EPOLLIN` = 关心"有数据可读"事件
- `EPOLLOUT` = 关心"可写"事件
- `EPOLLONESHOT` = 用一次剪线
- `EPOLLET` = 边沿触发模式

事件不是"何时"发生的(那是被动的,由数据到达决定),而是"我对哪类事件感兴趣"。

### 完整对应到代码

```cpp
epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);
//        ↑↑↑↑  ↑↑↑↑↑↑↑↑↑↑↑↑↑  ↑↑↑↑↑↑↑  ↑↑
//        哪个   做什么操作       哪扇门    关心什么+返回什么
//        epoll                            event.events + event.data
```

## ONESHOT:用一次剪线

### 普通 vs ONESHOT

```
普通卡片: 门开 → 扯线 → 卡片拎出 → 还能继续用,下次门开还会扯

ONESHOT 卡片: 门开 → 扯线 → 卡片拎出的同时,线被【剪断】
                ↓
            下次门开,没线可扯,卡片不会被拎出来
                ↓
            必须手动 epoll_ctl(MOD) 重新【绑一根新线】,才能再次响应
```

### 为什么必须剪线?

不剪(不加 ONESHOT)的后果:
- 工作线程还在处理第 1 条消息,客户端又发了第 2 条
- 门又开了,卡片又被拎出来,**扔给另一个工作线程**
- 两个工作线程同时操作同一个对象 → 数据错乱

剪线后:
- 处理期间,即使客户端再发数据,门开了也没线可扯
- 必须等当前线程处理完、MOD 重新绑线,才能再被拎出来
- **保证同一对象同一时刻只有一个线程在动**

## 剪线期间 epoll 对这个 fd 完全"失忆"

延续上面的比喻,剪线后发生的事很微妙,值得单独拎出来讲清楚。

### 时间线:从剪线到重新绑线之间发生了什么

```
T1: epoll_wait 上报 fd → 卡片拎出 + 线被自动剪断
T2: 工作线程拿到对象,开始执行 Do_client()
    │
    │  ←── 期间内核 epoll 对这个 fd 完全"失忆"
    │       因为 sk_wq 上没挂线了,卡片也不在监控状态
    │
T3: 客户端发来新数据
    │   ↓
    │   网卡 → 协议栈 → 数据丢进 socket 的 sk_receive_queue
    │   ↓
    │   协议栈按例调 wake_up 遍历 sk_wq
    │   ↓
    │   sk_wq 上空空如也(线刚被剪) → 没有回调可触发
    │   ↓
    │   epoll **不会知道**这件事,卡片不会被拎出来
    │   ↓
    │   epoll_wait **不会返回这个 fd** ★ 关键 ★
    │
T4: Do_client() 终于处理完
    │   ↓
T5: 调 epoll_ctl(MOD) 重新绑线
    │   ↓
    │   sk_wq 上又挂上 epoll 的线了
    │   ↓
    │   因为 socket 收件箱里有积压数据(level-triggered),
    │   绑线动作本身立刻触发 wake_up
    │   ↓
    │   线被扯动 → 卡片再次被拎出来
    │   ↓
T6: epoll_wait 这次才返回这个 fd → 又来一个工作线程处理
```

### 三个对象的归属(避免混淆)

| 对象 | 内核实体 | 归属 | 剪线后状态 |
|---|---|---|---|
| 房间 | `struct socket` | socket 自己 | 完好 |
| 房间里的收件箱 | `sk_receive_queue` | socket 自己 | 完好,继续接收数据 |
| 房间门铃按钮 | `sk_wq` | socket 自己 | 完好,照常被按 |
| **你绑的线** | `epitem.wait` + `ep_poll_callback` | **epoll 的** | **被剪掉了!** |

**关键**:剪线只剪 epoll 的那根线,socket 自己的"门+收件箱+门铃"都还在。

### 数据的归宿:不会丢,只是躺在收件箱

```
客户端发数据
    ↓
协议栈送进 socket 的 sk_receive_queue
    ↓
按门铃(wake_up)→ 没线 → epoll 不知道
    ↓
数据就静静躺在收件箱里
    ↓
等 Do_client 处理完,MOD 重新绑线
    ↓
level-triggered 模式下,收件箱有积压数据 → 立刻触发 wake_up
    ↓
卡片再次被拎出来 → epoll_wait 立刻返回 → recv() 把数据读走
```

### 关键确认

| 现象 | 是否发生 | 原因 |
|---|---|---|
| epoll 还在监控这个 fd | ❌ | sk_wq 上没有 epoll 的线,wake_up 触发不了 epoll 回调 |
| epoll_wait 会返回这个 fd | ❌ | 卡片没被拎进就绪堆 rdllist |
| epoll 主动重新加这个 fd | ❌ | epoll 是被动的,你不 MOD,它永远不"想起来"这个 fd |
| 数据丢失 | ❌ | 静静躺在 sk_receive_queue 里 |
| Do_client 做完 MOD 后能再次响应 | ✅ | MOD 重新挂线,积压数据立即触发 wake_up |

### 一句话总结

**剪线期间 epoll 对这个 fd 完全"失忆"——客户端发数据只进收件箱,不会触发 epoll_wait 上报;只有 Do_client 处理完调 MOD 重新绑线,积压数据才会立刻让卡片再次被拎出来。**

## 对应到代码

```cpp
// AccSocket::Do_client 里,初次绑线
ev.events = EPOLLIN | EPOLLONESHOT;   // 卡片标"用一次剪线"
ev.data.ptr = recvSock;
epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);  // 绑线

// 客户端发第1条消息
// → 门开 → 扯线 → 卡片拎出,线被剪断
// → epoll_wait 拿到卡片 → 你拿到 recvSock
// → 工作线程处理 recv

// RecvSocket::Do_client 末尾,重新绑线
ev.events = EPOLLIN | EPOLLONESHOT;   // 仍然标"用一次剪线"
ev.data.ptr = this;
epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);  // 重新绑线
```

## 完整生命周期

```
[AccSocket::Do_client 里]
   new RecvSocket(conn_fd, epfd)
   ev.events = EPOLLIN | EPOLLONESHOT   ← 注册时挂上 ONESHOT
   ev.data.ptr = recvSock
   epoll_ctl(ADD, conn_fd, ...)
       ↓
   conn fd 进 epoll 红黑树,等待数据
       ↓
[客户端第 1 次发消息]
   epoll_wait 上报 conn fd             ← ONESHOT 触发:用一次,立即禁用
   工作线程拿到 recvSock 对象
   recv + 处理
   MOD 重新武装(ev.data.ptr = this)   ← 又能被上报了
       ↓
[客户端第 2 次发消息]
   epoll_wait 上报 conn fd             ← 又被禁用一次
   工作线程拿到同一个 recvSock 对象
   recv + 处理
   MOD 重新武装
       ↓
   ... 反复 ...
       ↓
[客户端断开] recv <= 0 / "exit" / "bye"
   DEL conn fd
   delete this                         ← 对象销毁,生命周期结束
```

## epoll_wait 阻塞 vs CPU

- 没事件 → `epoll_wait` 阻塞休眠,让出 CPU,不是忙等
- 工作线程拿不到任务 → `pthread_cond_wait` 休眠,不占 CPU
- 客户端"不发消息"期间:
  - 主线程 `epoll_wait` 阻塞休眠 → 0% CPU
  - 工作线程 `cond_wait` 阻塞休眠 → 0% CPU
  - 内核监听 fd 挂在网卡中断上 → 不占 CPU

## 关键澄清

1. `epoll_wait` 上报 fd = fd 上有数据(就绪)
2. 工作线程被分配去 recv 时,数据早就在 fd 缓冲区,recv 立刻返回,不会阻塞
3. 客户端"不发消息"时,工作线程根本不会被分配(因为 epoll_wait 不上报)
4. ONESHOT 防止并发抢同一 fd,**不是防 Do_client 阻塞**(Do_client 本来就不会阻塞)

## epoll_event.data 是 union

```c
union epoll_data {
    void *ptr;      // 可以直接存对象指针
    int fd;
    uint32_t u32;
    uint64_t u64;
};
```

- 存 ptr:epoll_wait 拿到事件时,直接 `ev.data.ptr` 就是你的对象指针,**不用 new 新对象**
- 存 fd:epoll_wait 拿到事件时,只有 fd 这个整数,主线程还得自己 new 对象

**复用对象的关键**:ADD 时存 ptr,处理完 MOD 时也存 ptr(`this`),同一连接全程复用同一个对象。

## epoll 怎么找到正确的门(ADD 时 fd 怎么定位 socket)

epoll 不创造门,socket 创建时自带门铃。epoll 只是查 fd 表找到房间,把线挂到房间门铃上。

### fd 是房间号牌,不是房间

fd 只是个整数(比如 5),本身没意义。fd 是进程"文件描述符表"的索引,**通过 fd 查这张表才能找到真实的"房间"(socket 结构)**。

### ADD 时内核的查找路径

```
你说: epoll_ctl(ADD, fd=5, ...)
       ↓
内核拿 fd=5 去查【进程的 fd 表】         ← 大楼前台登记簿
       ↓
拿到对应的 struct file                   ← 房间登记卡
       ↓
file 里有 private_data 指针 → struct socket   ← 真实房间
       ↓
socket 里有【等待队列头 sk_wq】           ← 房间自带的门铃按钮
       ↓
内核把卡片的线(epitem.wait)挂到 sk_wq 上   ← 绑到门铃按钮上
```

### 数据到达时谁开门

```
网卡收到数据 → 协议栈处理 → 数据送到对应 socket 的接收队列
       ↓
协议栈处理完时,主动调用 socket.sk_wq 的 wake_up 函数   ← 按响门铃
       ↓
wake_up 遍历 sk_wq 上挂的所有线,挨个调用回调
       ↓
其中一根线就是 epitem.wait → ep_poll_callback 触发 → 卡片拎出
```

### 关键点

1. **门(socket 的 sk_wq)是 socket 自己的,不是 epoll 的**——每个 socket 创建时就自带门铃
2. **epoll 只是把自己的线挂到门上**——不创造门,只挂钩子
3. **开门的人是协议栈**——数据到达时,协议栈主动调用 wake_up 按门铃
4. **"fd → file → socket → sk_wq"的查找在 ADD 时做一次**,绑好线后,后续触发时根本不用再查 fd

### 类比对应表

| 比喻物 | 实际内核对象 |
|---|---|
| 房间号牌(如 305) | fd |
| 大楼前台登记簿 | 进程的 fd 表 |
| 房间登记卡 | struct file |
| 真实房间 | struct socket |
| 房间门铃按钮 | sk_wq(等待队列头) |
| 你绑的线 | epitem.wait + ep_poll_callback |
| 送快递的人 | 网卡 + 协议栈 |
| 按门铃 | wake_up 函数 |
| 卡片拎出 | epitem 加入就绪链表 rdllist |

## epoll_ctl 的 fd 参数 vs event.data 字段(常见混淆)

**epoll 不是通过你的 `data.ptr` 里的对象找 fd 的——fd 是 `epoll_ctl` 的第 3 个参数,你单独传的!**

### 看 epoll_ctl 的函数签名

```c
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
//                              ^^                  ^
//                              第 3 参数 fd          第 4 参数 event
//                              ↑↑↑↑↑↑↑↑↑↑         ↑↑↑↑↑↑↑↑↑↑
//                              epoll 用这个找门     data 只是寄存箱
```

epoll 用第 3 个参数 `fd` 去查 fd 表找门。第 4 个参数 `event` 里的 `data` 只是 epoll 的"备忘录"——你存什么 epoll 就存什么,**epoll 完全不解析它**,事件发生时原样还给你。

### 对应到代码

```cpp
ev_client_accpet.events = EPOLLIN | EPOLLONESHOT;
auto* recvSock = new RecvSocket(conn_fd, epfd);
ev_client_accpet.data.ptr = recvSock;          // ← 备忘录,存指针
                                                 //   epoll 不解析,只寄存

epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev_client_accpet);
//                              ↑↑↑↑↑↑↑
//                              ★ epoll 通过这个 conn_fd 找门!
//                              (跟你 data.ptr 里有没有 fd 成员无关)
```

epoll 是通过 **第 3 参数 `conn_fd`** 找 socket 门的,跟你 `data.ptr` 指向的对象里有什么成员**完全无关**。

### epoll 关心什么、不关心什么

| epoll 关心的 | epoll 不关心的 |
|---|---|
| 第 3 参数 fd(找门用) | data.ptr 指向什么类型的对象 |
| event.events(关心什么事件) | data.ptr 指向的对象里有什么成员 |
| 第 1 参数 epfd(哪个 epoll 实例) | 你的对象里有 fd 没 fd |

### 类比

- epoll_ctl(ADD, conn_fd, &ev) 像你去快递站登记
  - 你说:"我等 **305 号房间**的快递,这是我电话(存 ptr)" ← epoll 用 305 找门
  - 快递站不会问你电话里有什么、电话机里有什么 fd 信息
  - 电话(ptr)只是个寄存信息,快递到了原样还你
- epoll_wait 取快递 = 把你存的电话原样还给你

### 关键结论

**epoll 不需要从你的对象里读 fd**:
- 找门的工作用 `epoll_ctl` 第 3 参数 fd 完成
- `data.ptr` 只是个"附带信息包",epoll 不解析、不关心、不读取
- 你存 ptr 是为了**拿回对象指针方便多态调用**,不是为了告诉 epoll fd 在哪

## epoll 是"被动接收通知",不是"自动检测"

| 描述 | 准确吗 | 原因 |
|---|---|---|
| epoll 会自动检测 fd 上有没有数据 | ❌ | "检测"= 轮询,要主动去看每个 fd,效率低 |
| epoll 会自动**接收** fd 上有数据的通知 | ✓ | fd 有数据时,socket 主动通知 epoll |

### 流程

```
你: epoll_ctl(ADD, fd, ...)       ← 把 fd 告诉 epoll
       ↓
内核: 把卡片的线挂到 socket 的门铃上
       ↓
(epoll 后续什么都不做,安静等着)
       ↓
客户端发数据
       ↓
协议栈处理完数据 → 主动按 socket 的门铃(wake_up)
       ↓
门铃响 → 扯动线 → 卡片拎出来 → 放进就绪堆
       ↓
epoll_wait 取卡片 → 把 data 还给你
```

### 关键点

1. **你 ADD 后,epoll 不主动去检查 fd**——它只是把线挂到 socket 的门上,自己睡觉
2. **"盯着"的工作不在 epoll 这边,而在 socket 那边**——socket 收到数据会自己按门铃
3. **epoll 是被动接收者**,不是主动检测者
4. 这就是 epoll 比 select/poll 高效的根本原因——后两者每次都要遍历所有 fd 检查状态(轮询),epoll 靠回调零 CPU

### 类比

- 你把快递地址告诉快递站(ADD)
- 快递站不会每天去问"我的快递到了吗"(轮询)
- 快递到了,快递员自己打电话通知你(回调)
- 你接电话取快递(epoll_wait)

## 一句话总结

每张卡片绑根线系到对应 socket 门上,socket 来数据自动开门扯线,卡片被拎进就绪堆,`epoll_wait` 取卡片把上面的 data 还你。**关联性靠"那根线"建立,不靠查表**。

补充:**epoll 用 `epoll_ctl` 第 3 参数 fd 找门(`fd → file → socket → sk_wq`),`data.ptr` 只是个寄存箱,epoll 不解析只搬运;epoll 是被动接收通知(回调),不是主动检测(轮询)**。
