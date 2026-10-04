# 多线程池库（header-only）

一个纯头文件的 **C++17** 线程池实现，一次实现 **5 种调度策略**，配一份完整的**抗压测试报告**（4 个场景、累计 280 万+ 任务、零丢失零崩溃）。

所有类型统一在命名空间 `ctx` 下，只需 include 两个头文件即可使用。

## 五种线程池

| 线程池 | 调度策略 | 适合什么场景 |
|---|---|---|
| `FixedThreadPool` | 固定 N 个 worker 常驻，共享一条任务队列 | 负载稳定、对延迟敏感 |
| `CacheThreadPool` | 按需创建线程，空闲 60ms 回收（独立管理线程 `Sudo()` 负责扩缩容） | 流量潮汐明显的场景 |
| `SignalThreadPool` | 单线程串行执行，严格保序 | 不能并发、要求顺序执行的任务 |
| `ScheduledThreadPool` | 优先队列 + `wait_until`，支持延时任务 | 定时任务 |
| `WorkStealingThreadPool` | 每线程一条**独立队列 + 独立锁**，空闲时去"偷"别人的任务 | 高吞吐、CPU 密集 |

## 目录结构

```
threadpool/
├── CMakeLists.txt          # 顶层构建（产物输出到 bin/）
├── include/
│   ├── TaskQueue.hpp       # 任务队列：deque（普通）+ priority_queue（定时）+ 工作窃取取任务逻辑
│   └── ThreadPool.hpp      # 5 种线程池实现
├── main/
│   ├── CMakeLists.txt
│   └── test.cpp            # 4 场景自动化压测程序
├── 线程池设计文档.md        # 含两个头文件的完整源码 + 设计思考原文
└── 抗压测试报告.md          # 4 场景实测数据与问题清单
```

## 关键设计

### 1. 任务队列：两种容器，一个抽象

`Taskdeque` 用 `std::deque` 承载普通任务（FIFO），`PriorityTaskqueue` 用 `std::priority_queue` + 自定义比较器 `CompareTask` 承载定时任务，按到期时间排序。定时池复用前者做就绪队列、后者做延迟队列。

### 2. 队列满时做背压，而不是拒绝任务

队列容量固定为 `MaxTaskCount`（默认 200）。满了以后提交端在 `m_notFull` 条件变量上等待，而不是丢弃或抛异常——**压测中 100 万个任务全部执行完毕、零丢失**，就是这条在兜底。

### 3. 工作窃取为什么快 2.7 倍

其余多线程池**共用一把队列锁**，且每次入队都 `notify_all`，8 个 worker 被同时唤醒后抢同一把锁（惊群），这是它们 8 核下加速比只有 2.6~2.9× 的根本原因。

`WorkStealingThreadPool` 换了个思路：

- `AddTask` 轮询分发，把竞争分散到 **8 把锁**上
- worker 优先消费自己的本地队列，**几乎无锁竞争**
- 自己队列空了，才去偷相邻线程的（窃取时才触碰对方的锁）

结果：8 核上跑到 **10.9× 加速比**（超线性，因为消除了伪共享级的锁争抢）。

### 4. 先建容器，后建线程

线程启动后要互相访问"相邻线程对象"的地址。如果边 `push_back` 边启动线程，`vector` 扩容会让先前传进去的地址全部失效。所以严格分两步：**容器先建满、定下最终地址，再创建线程**。

### 5. 头文件自包含

`TaskQueue.hpp` 必须自己 `#include <functional>`（`using Task = std::function<void(void)>` 需要它）。否则一旦它被先于 `<functional>` include，`Task` 未声明会引发一串连锁报错。

## 实测数据

> 环境：Intel i7-14650HX（虚拟机分配 **8 核**）· 3.8 GiB · Ubuntu 20.04 / 内核 5.15 · g++ 9.4.0 · `-O2 -std=c++17 -Wall`
> 配置：worker 上限 8（= `hardware_concurrency`）· 队列容量 200 · Cache/Scheduled 初始 1 线程、空闲 60ms 回收

### 场景 1：轻量任务（100 万空任务，8 生产者并发提交）

| 线程池 | 吞吐 TPS | 平均延迟 | P50 | P99 | 最大延迟 | 丢失 |
|---|---:|---:|---:|---:|---:|---:|
| **WorkStealing** | **1,879,155** | 169.8μs | 94μs | 1,263μs | 9.4ms | 0 |
| Fixed | 684,278 | 153.4μs | 75μs | 1,206μs | 24.6ms | 0 |
| Cache | 597,706 | 205.8μs | 109μs | 1,757μs | 29.9ms | 0 |
| Scheduled | 466,927 | 289.6μs | 197μs | 2,000μs | 37.8ms | 0 |
| Signal | 345,034 | 497.8μs | 305μs | 4,260μs | 49.2ms | 0 |

**100 万任务全部完成、零丢失**；所有池的 drain 时间 ≈ 0，说明消费能跟上提交。

### 场景 2：CPU 密集（20 万任务 × 3000 次 `sqrt`）

| 线程池 | 吞吐 TPS | 相对单线程加速比 |
|---|---:|---:|
| **WorkStealing** | **307,568** | **10.9×** |
| Cache | 81,969 | 2.9× |
| Scheduled | 80,715 | 2.8× |
| Fixed | 73,735 | 2.6× |
| Signal | 28,306 | 1.0×（基准） |

### 场景 3：定时精度（`ScheduledThreadPool`，1 万延时任务，延时 50~500ms）

平均误差 971.9μs，**P50 误差仅 360μs**，**提前执行 0 / 10000**——定时语义完全正确。尾部误差偏大（P99 10.6ms / Max 34.6ms），原因是大量任务相近时刻到期时多个 worker 被同一条件变量唤醒后抢锁，惊群放大了尾部延迟。

### 场景 4：突发流量与扩缩容（3 波 × 3 万，波间空闲 900ms）

- **Cache**：空闲 900ms（> 60ms 超时）后线程确实全部回收；新流量到达后管理线程逐个重建，约 **100~150ms** 爬升到满速。
- **Fixed**：线程常驻，突发开局即 40 万/s 满速，代价是空闲时 8 线程仍占用资源。
- 两者总耗时接近（3.028s vs 2.971s），差异集中在每波前 100ms 的爬坡。

### 内存

全程峰值 RSS 仅 **约 13 MB**（含压测程序自身 100 万 × 8B 的延迟采样数组）。池销毁后 RSS 不增长，**无泄漏迹象**。

## 压测暴露出的问题

报告第五节列了 6 条，按优先级：

1. **【惊群】** 共享队列的 `Add()` 每次入队 `notify_all` 唤醒全部等待线程 → 生产/消费各改 `notify_one` 即可。**这是 Fixed/Cache/Scheduled 加速比上不去的主因。**
2. **【参数失效】** `FixedThreadPool` / `CacheThreadPool` 的 `deque_size` 构造参数没生效——成员 `m_taskqueue` 是默认构造的，传入的值只赋给了同名成员变量，没传给队列。应改为 `m_taskqueue(deque_size)`（Signal / Scheduled / WorkStealing 已正确传递）。
3. **【CPU 空转】** Cache/Scheduled 的管理线程 `Sudo()` 在队列空时 `yield()` 死循环，空闲时吃满一个核 → 改条件变量等待或 `sleep_for(1ms)` 节流。
4. **【偷取延迟】** WorkStealing 空闲线程 50ms 轮询一次，低负载突发时最坏要 50ms 才偷到 → 可在 `AddTask` 后对目标队列做一次 notify。
5. **【日志噪音】** 停止时每个工作线程打印 `task deque status = false`，应降级为调试日志。
6. **【头文件结构】** `WorkStealingThreadPool` 定义在 `ThreadPool.hpp` 的 `#endif` 之后，脱离 include guard 保护，被多个编译单元包含时有重定义风险 → 应移入 `#endif` 之前。

## 编译运行

```bash
mkdir -p build && cd build
cmake .. && make
../bin/test          # 产物在 bin/test
```

或一条命令：`cmake --build build && ./bin/test`

## 相关文档

| 文件 | 内容 |
|---|---|
| [线程池设计文档.md](线程池设计文档.md) | `TaskQueue.hpp` / `ThreadPool.hpp` 完整源码 + 设计思考原文（注释嵌在源码对应位置） |
| [抗压测试报告.md](抗压测试报告.md) | 4 场景完整数据、内存占用、问题清单与复现命令 |

CSDN 博客（[一只旭宝](https://blog.csdn.net/2501_90355051)）上有对应的图文版：

- [五种线程池设计](https://blog.csdn.net/2501_90355051/article/details/164269398)
- [C++11 线程库 与 Linux pthread 线程库 对比](https://blog.csdn.net/2501_90355051/article/details/164187232)
- [细讲C++【8】：多线程下的互斥锁与条件变量常见误区](https://blog.csdn.net/2501_90355051/article/details/163566801)
