# C++11 线程库 与 Linux pthread 线程库 对比文档

> 对比对象：
> - **C++11 标准线程库**：`<thread>`、`<mutex>`、`<condition_variable>`、`<future>`（跨平台，语言级标准）
> - **Linux pthread 线程库**：`<pthread.h>`（POSIX 标准，Linux/Unix 平台，C 语言 API）

---

## 一、总体对比

| 维度 | C++11 线程库 | Linux pthread |
|------|--------------|---------------|
| 标准 | ISO C++ 语言标准，跨平台（Linux/Windows/macOS） | POSIX 标准，Unix/Linux 系平台 |
| 语言风格 | C++ 面向对象 + RAII，自动资源管理 | C 语言函数 + 句柄结构体，手动 init/destroy |
| 错误处理 | 抛异常（`std::system_error`） | 返回错误码（成功返回 0） |
| 编译 | `-std=c++11`（Linux 上底层仍是 pthread，需 `-lpthread`） | `-lpthread` |
| 头文件 | `<thread>` `<mutex>` `<condition_variable>` | `<pthread.h>` |
| 资源释放 | 析构函数自动完成（RAII 核心） | 必须手动调用 destroy，漏掉即泄漏/未定义行为 |
| 可读性 | 类型安全、模板化、可传任意可调用对象 | 只能传 `void* (*)(void*)`，参数要强转 `void*` |

**一句话总结**：pthread 是"手动挡"——每一把锁、每一个条件变量都要手动 init/destroy/lock/unlock；C++11 线程库是"自动挡"——对象构造即初始化，离开作用域析构自动释放，锁靠 RAII 自动解锁。

---

## 二、线程创建与销毁

### API 对照表

| 功能 | pthread | C++11 |
|------|---------|-------|
| 创建线程 | `pthread_create(&tid, attr, fn, arg)` | `std::thread t(fn, args...)` |
| 等待线程结束 | `pthread_join(tid, &retval)` | `t.join()` |
| 分离线程 | `pthread_detach(tid)` | `t.detach()` |
| 判断可否 join | 无（需自己记录状态） | `t.joinable()` |
| 获取线程 ID | `pthread_self()` | `std::this_thread::get_id()` / `t.get_id()` |
| 让出 CPU | `sched_yield()` | `std::this_thread::yield()` |
| 线程休眠 | `usleep()` / `nanosleep()` | `std::this_thread::sleep_for()` / `sleep_until()` |
| 线程数量 | `sysconf(_SC_NPROCESSORS_ONLN)` | `std::thread::hardware_concurrency()` |
| 退出线程 | `pthread_exit()` | 函数 `return`（或抛异常） |

### 写法对比

**pthread 版：**

```c
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

void* worker(void* arg) {
    int id = *(int*)arg;          // 参数必须通过 void* 传入并强转
    printf("thread %d running\n", id);
    int* ret = malloc(sizeof(int));
    *ret = id * 2;                // 返回值也只能通过 void* 带出
    return ret;
}

int main() {
    pthread_t tid;
    int id = 1;
    if (pthread_create(&tid, NULL, worker, &id) != 0) {  // 返回错误码
        perror("pthread_create failed");
        exit(1);
    }
    void* retval;
    pthread_join(tid, &retval);
    printf("result = %d\n", *(int*)retval);
    free(retval);                 // 带出的堆内存还要手动释放
    return 0;
}
// 编译: gcc demo.c -o demo -lpthread
```

**C++11 版：**

```cpp
#include <thread>
#include <iostream>

int worker(int id) {              // 参数就是普通函数参数，类型安全
    std::cout << "thread " << id << " running\n";
    return id * 2;                // 返回值就是普通返回值
}

int main() {
    std::thread t(worker, 1);     // 可传任意可调用对象：函数、lambda、仿函数
    int result = 0;
    // join 本身不取返回值；要取返回值用 std::future（见第八节）
    t.join();                     // t 析构前必须 join() 或 detach()，否则 std::terminate
    std::cout << "result = " << result << "\n";
    return 0;
}
// 编译: g++ -std=c++11 demo.cpp -o demo -lpthread
```

**要点差异：**

1. **线程入口签名**：pthread 强制 `void* fn(void*)`，传参/取返回值都要 `void*` 强转，类型不安全；C++11 是模板，任意签名都行，还能直接传 lambda：
   ```cpp
   std::thread t([](int x) { std::cout << x << "\n"; }, 42);
   ```
2. **生命周期规则**：`std::thread` 对象析构时如果仍 `joinable()`（既没 join 也没 detach），会直接调用 `std::terminate()` 杀掉整个进程——这是最常见的新手坑。pthread 的 `pthread_t` 只是个句柄，不 join 也不会"杀进程"，只是资源泄漏。
3. **move 语义**：`std::thread` 只能移动不能拷贝（`t2 = std::move(t1)`），把线程所有权在对象间转移；pthread 的 `pthread_t` 本身可随意拷贝（只是句柄复制，无所有权概念）。

---

## 三、互斥锁（mutex）

### API 对照表

| 功能 | pthread | C++11 |
|------|---------|-------|
| 定义 | `pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;` | `std::mutex m;` |
| 初始化 | `pthread_mutex_init(&m, &attr)` | 构造函数自动完成 |
| 加锁 | `pthread_mutex_lock(&m)` | `m.lock()`（但通常不用，见下） |
| 尝试加锁 | `pthread_mutex_trylock(&m)` | `m.try_lock()` |
| 解锁 | `pthread_mutex_unlock(&m)` | `m.unlock()` |
| 销毁 | `pthread_mutex_destroy(&m)` | 析构函数自动完成 |
| RAII 加锁 | **无**（手动 lock/unlock，容易漏 unlock） | `std::lock_guard<std::mutex>` / `std::unique_lock` |
| 递归锁 | `pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE)` | `std::recursive_mutex` |
| 超时锁 | `pthread_mutex_timedlock(&m, &timeout)` | `std::timed_mutex` / `std::recursive_timed_mutex` 的 `try_lock_for()` |

### 写法对比

**pthread 版（手动挡）：**

```c
pthread_mutex_t m;

// 使用前必须初始化
pthread_mutex_init(&m, NULL);

void* worker(void* arg) {
    pthread_mutex_lock(&m);       // 手动加锁
    // ... 临界区 ...
    if (some_error) {
        return NULL;              // BUG！提前 return 没解锁 -> 死锁
    }
    pthread_mutex_unlock(&m);     // 手动解锁
    return NULL;
}

// 程序结束前必须销毁
pthread_mutex_destroy(&m);
```

**C++11 版（自动挡 + RAII）：**

```cpp
std::mutex m;                     // 构造即初始化，无需 init

void worker() {
    {
        std::lock_guard<std::mutex> lock(m);   // 构造时加锁
        // ... 临界区 ...
        if (some_error) {
            return;               // 安全！lock_guard 析构自动解锁
        }
    }                             // 作用域结束自动解锁
    // 任何路径都不会漏解锁
}                                 // m 析构自动销毁，无需 destroy
```

**要点差异：**

1. **RAII 是最大优势**。pthread 手动 lock/unlock 在多分支/提前 return/抛错场景极易漏掉 unlock 导致死锁；`std::lock_guard` 利用对象析构保证任何退出路径都解锁。
2. **两种 RAII 锁的分工**：
   - `std::lock_guard`：轻量，构造加锁、析构解锁，不能中途 unlock，适合简单临界区；
   - `std::unique_lock`：更灵活，可提前 `unlock()`、可重新 `lock()`、可移动，**是 `condition_variable::wait()` 的必需参数**。
3. **pthread 也有 `PTHREAD_MUTEX_INITIALIZER` 静态初始化**，但动态创建的锁（如结构体成员）仍要走 init/destroy 流程；C++11 里锁就是普通对象，放进类里跟着对象生灭，心智负担为零。

---

## 四、条件变量（condition variable）

### API 对照表

| 功能 | pthread | C++11 |
|------|---------|-------|
| 定义 | `pthread_cond_t c = PTHREAD_COND_INITIALIZER;` | `std::condition_variable cv;` |
| 初始化 / 销毁 | `pthread_cond_init` / `pthread_cond_destroy` | 构造 / 析构自动 |
| 等待 | `pthread_cond_wait(&c, &m)` | `cv.wait(lock, pred)` |
| 限时等待 | `pthread_cond_timedwait(&c, &m, &abstime)` | `cv.wait_for(lock, dur)` / `wait_until()` |
| 唤醒一个 | `pthread_cond_signal(&c)` | `cv.notify_one()` |
| 唤醒全部 | `pthread_cond_broadcast(&c)` | `cv.notify_all()` |
| 配套锁类型 | 只能配 `pthread_mutex_t` | 必须配 `std::unique_lock<std::mutex>` |

### 写法对比

**pthread 版：**

```c
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  c = PTHREAD_COND_INITIALIZER;
int task_num = 0;
bool running = true;

void* worker(void* arg) {
    pthread_mutex_lock(&m);
    // pthread_cond_wait 存在虚假唤醒，必须自己写 while 循环判断条件
    while (task_num == 0 && running) {
        pthread_cond_wait(&c, &m);    // 内部：解锁 -> 挂起 -> 被唤醒后重新加锁
    }
    if (running) {
        task_num--;
    }
    pthread_mutex_unlock(&m);
    return NULL;
}

// 生产者
void push_task() {
    pthread_mutex_lock(&m);
    task_num++;
    pthread_cond_signal(&c);          // 唤醒一个
    pthread_mutex_unlock(&m);
}
```

**C++11 版：**

```cpp
std::mutex m;
std::condition_variable cv;
int task_num = 0;
bool running = true;

void worker() {
    std::unique_lock<std::mutex> lock(m);   // wait 必须用 unique_lock
    // 带谓词的 wait：内部自动循环，虚假唤醒不用自己管
    cv.wait(lock, [] { return task_num != 0 || !running; });
    if (running) {
        task_num--;
    }
    // lock 离开作用域自动解锁
}

// 生产者
void push_task() {
    {
        std::lock_guard<std::mutex> lock(m);
        task_num++;
    }                                     // 先解锁再 notify，避免被唤醒方拿不到锁
    cv.notify_one();                      // 唤醒一个
}
```

**要点差异：**

1. **虚假唤醒处理**：pthread 的 `pthread_cond_wait` 必须外面套 `while` 判断条件；C++11 的 `cv.wait(lock, pred)` 把 while 循环内置了，**谓词版本直接消灭了虚假唤醒这个经典坑**。
2. **限时等待返回值**：pthread 的 `timedwait` 用绝对时间 `struct timespec`，要手动算超时点；C++11 的 `wait_for(std::chrono::seconds(2))` 直接写相对时长，且返回 `std::cv_status::timeout / no_timeout` 枚举，清晰得多。
3. **锁的搭配**：C++11 条件变量只能配 `std::unique_lock`（因为 wait 中途要解锁/加锁，lock_guard 做不到）；pthread 配的是裸 mutex 句柄。

---

## 五、读写锁 与 其他锁类型

| 功能 | pthread | C++11 |
|------|---------|-------|
| 读写锁 | `pthread_rwlock_t`（`rdlock`/`wrlock`/`unlock`） | `std::shared_mutex`（C++17；`shared_lock` 读 / `lock` 写） |
| 自旋锁 | `pthread_spinlock_t` | **无标准对应**（可用原子操作 `std::atomic_flag` 自己实现） |
| 一次性初始化 | `pthread_once_t` + `pthread_once(&once, fn)` | `std::once_flag` + `std::call_once(flag, fn)` |
| 屏障 | `pthread_barrier_t` | **无标准对应**（需自己用 mutex+cond 实现） |
| 信号量 | `sem_t`（`<semaphore.h>`） | **无标准对应**（C++20 才有 `std::counting_semaphore`） |

> 说明：读写锁在 C++14/17 才补齐（`std::shared_timed_mutex` 是 C++14，`std::shared_mutex` 是 C++17），C++11 标准里没有，用 C++11 只能退回 `std::mutex`。

---

## 六、线程局部存储（TLS）

| 功能 | pthread | C++11 |
|------|---------|-------|
| 声明 | `__thread int x;`（GCC 扩展）或 `pthread_key_t` + `pthread_key_create`/`pthread_getspecific`/`pthread_setspecific` | `thread_local int x;`（语言关键字） |
| 析构回调 | `pthread_key_create(&key, destructor)` 手动注册 | 变量析构自动调用 |

**写法对比：**

```c
// pthread: 每线程一份错误码
__thread int per_thread_errno;
```

```cpp
// C++11: 关键字直接声明，还能是类类型（pthread 的 __thread 只能是 POD）
thread_local std::string per_thread_name;
```

C++11 的 `thread_local` 支持**非平凡类型**（有构造/析构函数的类对象），并且析构自动执行；pthread 的 TLS 值即使是类对象也不会自动析构（除非手动注册 destructor），且 `__thread` 是编译器扩展不是标准。

---

## 七、future / async：C++11 独有的高层封装

pthread 完全没有对应物。想拿到线程函数的**返回值**，pthread 只能自己 `malloc` 一块内存塞进 `void*`，join 后取出来再 `free`；C++11 用 `std::future` 三行搞定：

```cpp
#include <future>

int worker(int x) { return x * 2; }

int main() {
    // async 启动异步任务，返回 future
    std::future<int> f = std::async(std::launch::async, worker, 21);
    int result = f.get();             // 阻塞等待并取回返回值，无需手动 join
    // result == 42
}
```

| 功能 | 说明 |
|------|------|
| `std::async` | 把函数扔到后台线程（或同线程延迟）执行，返回 `future` |
| `future::get()` | 阻塞取返回值（或重新抛出线程里的异常！） |
| `future::wait_for()` | 带超时等待 |
| `std::promise` | 手动在线程间传递"一次性的值"，配合 future 使用 |
| `std::packaged_task` | 把可调用对象包装成可异步取结果的任务 |

**特别有价值的一点**：工作线程里抛出的异常会被 future 捕获，在 `get()` 时于调用方线程重新抛出——pthread 里线程函数抛异常直接 `std::terminate`，根本传不出来。

---

## 八、完整小例子对比：生产者-消费者（3 生产 + 消费模型核心片段）

**pthread 版（生产者消费者骨架）：**

```c
// 需要维护：pthread_mutex_t、pthread_cond_t、手动 init、
// worker 里 while 判虚假唤醒、手动 lock/unlock、退出时 broadcast + join + destroy
pthread_mutex_lock(&m);
while (queue_empty && running) {
    pthread_cond_wait(&not_empty, &m);
}
if (!running) { pthread_mutex_unlock(&m); return NULL; }
task = pop_queue();
pthread_mutex_unlock(&m);
process(task);
```

**C++11 版：**

```cpp
std::unique_lock<std::mutex> lock(m);
cv.wait(lock, [] { return !queue.empty() || !running; });  // 谓词内置循环
if (!running) return;
auto task = queue.front(); queue.pop();
lock.unlock();                                              // 可中途解锁
process(task);
```

同样的逻辑，C++11 版代码量约为 pthread 版的 60%~70%，且消灭了"漏 unlock"、"漏 destroy"、"虚假唤醒"、"忘 broadcast"四大经典坑。

---

## 九、坑位对照总结

| 经典坑 | pthread 表现 | C++11 表现 |
|--------|--------------|------------|
| 忘 unlock | 死锁 | `lock_guard`/`unique_lock` RAII 兜底，不可能漏 |
| 忘 destroy | 资源泄漏 | 析构自动完成 |
| 虚假唤醒 | 必须手写 `while` 循环 | `wait(lock, pred)` 内置处理 |
| 提前 return 逃出临界区 | 死锁 | RAII 析构自动解锁 |
| 线程函数返回值传递 | `void*` + 堆内存 + 手动 free | `std::future::get()` 直接拿 |
| 线程内异常传播 | 直接 terminate，无法传出 | future 会把异常搬到调用方重抛 |
| 忘记 join/detach | `pthread_t` 只是句柄，泄漏但不崩 | `std::thread` 析构时仍 joinable 会 `std::terminate`（新坑，需注意） |
| 参数生命周期 | 传 `void*` 指针，线程还没读参数主线程就改了 -> 悬垂 | 传值会拷贝进线程，天然安全；传引用要用 `std::ref` 且自己保证生命周期 |

---

## 十、选型建议

1. **新写 C++ 项目**：直接用 C++11 线程库。类型安全、RAII、跨平台，没有理由再手写 pthread。
2. **维护老 C 代码 / C 项目**：只能用 pthread。
3. **混合使用完全可行**：同一进程里 pthread 线程和 `std::thread` 线程可以共存互操作（Linux 上 `std::thread` 底层就是 `pthread_create`），`std::thread::native_handle()` 还能取出底层 `pthread_t` 去设置调度策略等 pthread 特有属性。
4. **链接注意**：Linux 上用 C++11 线程库仍然要 `-lpthread`（glibc 2.34+ 已并入 libc，可不加，但加上总没错），编译要 `-std=c++11` 或更高。
5. **需要信号量/屏障/自旋锁**：C++11 标准库没有，用 pthread 的 `sem_t`/`pthread_barrier_t`，或基于 `std::mutex + condition_variable` / `std::atomic` 自行封装（C++20 才补上 `std::counting_semaphore`、`std::latch`、`std::barrier`）。

---

## 附：API 速查总表

| 功能类别 | pthread | C++11 |
|----------|---------|-------|
| 创建 | `pthread_create` | `std::thread` 构造 |
| 等待 | `pthread_join` | `join()` |
| 分离 | `pthread_detach` | `detach()` |
| 线程 ID | `pthread_self` | `get_id()` |
| 休眠 | `nanosleep` | `sleep_for` |
| 让出 | `sched_yield` | `yield()` |
| CPU 数 | `sysconf` | `hardware_concurrency()` |
| 互斥锁 | `pthread_mutex_t` (init/lock/trylock/unlock/destroy) | `std::mutex` + `lock_guard`/`unique_lock` |
| 递归锁 | `PTHREAD_MUTEX_RECURSIVE` | `std::recursive_mutex` |
| 超时锁 | `pthread_mutex_timedlock` | `std::timed_mutex::try_lock_for` |
| 条件变量 | `pthread_cond_t` (wait/timedwait/signal/broadcast) | `std::condition_variable` (wait/wait_for/notify_one/notify_all) |
| 读写锁 | `pthread_rwlock_t` | `std::shared_mutex` (C++17) |
| 一次性 | `pthread_once` | `std::call_once` |
| TLS | `pthread_key_*` / `__thread` | `thread_local` |
| 异步结果 | 无 | `std::async`/`future`/`promise`/`packaged_task` |
| 信号量 | `sem_t` | 无（C++20 `counting_semaphore`） |
| 屏障 | `pthread_barrier_t` | 无（C++20 `barrier`） |
| 自旋锁 | `pthread_spinlock_t` | 无（用 `atomic_flag` 自实现） |
