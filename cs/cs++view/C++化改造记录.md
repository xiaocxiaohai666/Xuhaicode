# C++ 化改造记录

> 本文档记录从 `csproject2view` 拷贝到 `cs++view` 后，将原 C/pthread 语法彻底 C++11 化的所有改动点。每处改动均带源码行号引用，便于核对。

---

## 一、改造目标

将 `service.h` / `service.cpp` / `client.h` 中所有 **C 风格** 同步原语与内存管理，替换为 **C++11 标准库** 等价设施：

| 维度 | 原 C/pthread | C++11 化后 |
|------|--------------|------------|
| 头文件 | `<pthread.h>` `<stdlib.h>` | `<thread>` `<mutex>` `<condition_variable>` `<cstdlib>` |
| 互斥锁 | `pthread_mutex_t` + 手动 init/lock/unlock/destroy | `std::mutex` + `std::lock_guard`/`std::unique_lock`（RAII） |
| 条件变量 | `pthread_cond_t` + 手动 init/wait/signal/broadcast/destroy | `std::condition_variable` + `wait(pred)`/`notify_one/all` |
| 线程 | `pthread_t[]` + `pthread_create`/`pthread_join` | `std::thread[]` + move assign + `join()` |
| 内存 | `malloc` / `free` | `new[]` / `delete[]`（值初始化） |
| 线程入口 | `void* fn(void* arg)` 返回 `void*` | `void fn(Thread_Pool*)` 无返回值 |
| 结构体 | `typedef struct {} name;` | `struct name {};` |

---

## 二、service.h 改动（8 处）

### 2.1 头文件
[service.h:1](service.h#L1) `<stdlib.h>` → `<cstdlib>`

[service.h:7-9](service.h#L7-L9) 移除 `<pthread.h>`，新增：
```cpp
#include <thread>
#include <mutex>
#include <condition_variable>
```

### 2.2 结构体语法
[service.h:88-90](service.h#L88-L90) `typedef struct { Socket* task; } task_t;` → `struct task_t { Socket* task; };`

### 2.3 Thread_Pool 类成员
[service.h:98-100](service.h#L98-L100) 三个 C 同步原语 → C++ 对象：
```cpp
std::mutex mutex;                       // 替代 pthread_mutex_t
std::condition_variable cond;           // 替代 pthread_cond_t
std::thread Thread_array[thread_num];   // 替代 pthread_t[]
```

### 2.4 General_Thread 函数签名
[service.h:111](service.h#L111) `void* General_Thread(void* arg)` → `void General_Thread(Thread_Pool* pool)`

### 2.5 Connect_Pool 类成员
[service.h:169](service.h#L169) `pthread_mutex_t mutex` → `std::mutex mutex`

---

## 三、service.cpp 改动（按函数逐一，共 29 处）

### 3.1 移除 C 头文件
[service.cpp:10](service.cpp#L10) 删除 `#include <pthread.h>`（`<thread>` 等已由 service.h 提供）

### 3.2 Thread_Pool 析构函数
[service.cpp:276-288](service.cpp#L276-L288)
```cpp
Thread_Pool::~Thread_Pool() {
  {
    std::lock_guard<std::mutex> lock(mutex);   // L278 替代 lock
    status = false;
  }
  cond.notify_all();                           // L281 替代 cond_broadcast
  for (int i = 0; i < thread_num; i++) {
    if (Thread_array[i].joinable())            // L284 joinable 检查
        Thread_array[i].join();                //      替代 pthread_join
  }
  delete[] dequeue;                            // L286 替代 free
  // mutex/cond 对象析构自动清理，无需 destroy  L287
}
```

### 3.3 Thread_Pool::Init_Thread_pool
[service.cpp:290-308](service.cpp#L290-L308)
```cpp
dequeue = new task_t[d_size]();   // L292 new[] 替代 malloc，加 () 值初始化
// L305 mutex/cond 默认构造，无需 init
```

### 3.4 Thread_Pool::add_Task
[service.cpp:313-328](service.cpp#L313-L328)
```cpp
std::unique_lock<std::mutex> lock(mutex);   // L315 替代 lock
// ... 临界区 ...
cond.notify_one();                           // L326 替代 cond_signal
// unique_lock 析构自动释放，无需 unlock
```

### 3.5 General_Thread 线程入口
[service.cpp:330-333](service.cpp#L330-L333)
```cpp
void General_Thread(Thread_Pool *pool) {   // 不再需要 void* arg 和返回 void*
  pool->Work();
}
```

### 3.6 Thread_Pool::Create_Thread
[service.cpp:335-339](service.cpp#L335-L339)
```cpp
Thread_array[i] = std::thread(General_Thread, this);   // L337 move assign 替代 pthread_create
```

### 3.7 Thread_Pool::Work 工作循环
[service.cpp:341-360](service.cpp#L341-L360)
```cpp
std::unique_lock<std::mutex> lock(mutex);                       // L343
cond.wait(lock, [this]{ return task_Num != 0 || status == false; });  // L346 带谓词 wait，自动处理虚假唤醒
// ...
lock.unlock();   // L352 执行 Do_client 前释放
// 函数退出时 unique_lock 析构自动释放
```

### 3.8 Mysql_Client::Connect_Mysql_Server 借连接
[service.cpp:470-495](service.cpp#L470-L495)
```cpp
std::lock_guard<std::mutex> lock(g_pool->mutex);   // L475 替代 lock
// 所有 return 分支自动释放，无需手动 unlock（原代码有 2 处 unlock）
```

### 3.9 Mysql_Client 析构归还连接
[service.cpp:734-743](service.cpp#L734-L743)
```cpp
std::lock_guard<std::mutex> lock(g_pool->mutex);   // L736 替代 lock
// 析构自动释放，无需 unlock
```

### 3.10 Connect_Pool 构造函数
[service.cpp:758-760](service.cpp#L758-L760)
```cpp
// L759 std::mutex 默认构造，移除了 pthread_mutex_init
```

### 3.11 Connect_Pool 析构函数
[service.cpp:810-812](service.cpp#L810-L812)
```cpp
delete[] C_P_array;   // L811 new[] 配对 delete[]（原为 delete，已修正配对）
// L812 std::mutex 析构自动清理，移除了 pthread_mutex_destroy
```

---

## 四、client.h 改动（3 处）

[client.h:1](client.h#L1) `<stdlib.h>` → `<cstdlib>`

[client.h](client.h) 移除 `#include <pthread.h>`（客户端不使用线程）

[client.h:18](client.h#L18) `#define PORTADRESS 9999` → `9527`（配合端口替换）

> `client.cpp` 本身已是 C++ 风格（使用 `std::string`、`Json::Value`、`cout`、类封装），无 `malloc`/`pthread`，无需改动。

---

## 五、Makefile 改动

[Makefile](Makefile) 编译选项加 `-std=c++11`（`std::thread`/`std::mutex`/`std::condition_variable` 强制要求）：
```makefile
g++ -std=c++11 -g -O0 -o service service.cpp -lpthread -lmysqlclient -ljsoncpp
g++ -std=c++11 -g -O0 -o client client.cpp -lpthread -lmysqlclient -ljsoncpp
```

> `-lpthread` 仍保留：`std::thread` 在 Linux 上的底层实现仍是 pthread，需要链接该库；但**代码层面**已彻底 C++ 化，无任何 pthread 函数调用。

---

## 六、端口替换（59 处）

| 端口 | 原值 | 新值 | 涉及文件 |
|------|------|------|---------|
| C++ 服务 | 9999 | **9527** | `service.conf`、`client.h`、`web/app.py`、`web/cp_client.py`、`start_all.sh`、`stop_all.sh`、`预约系统版本2.md` |
| Flask Web | 5000 | **5500** | `web/app.py`、`start_all.sh`、`stop_all.sh`、`预约系统版本2.md` |

浏览器访问地址：`http://localhost:5500`

---

## 七、改造前后对照总表

| 序号 | 原 C/pthread 写法 | C++11 写法 | 所在位置 |
|------|------------------|-----------|---------|
| 1 | `#include <pthread.h>` | `<thread>` `<mutex>` `<condition_variable>` | service.h:7-9 |
| 2 | `#include <stdlib.h>` | `#include <cstdlib>` | service.h:1, client.h:1 |
| 3 | `typedef struct {} task_t;` | `struct task_t { ... };` | service.h:88-90 |
| 4 | `pthread_mutex_t mutex` | `std::mutex mutex` | service.h:98, 169 |
| 5 | `pthread_cond_t cond` | `std::condition_variable cond` | service.h:99 |
| 6 | `pthread_t Thread_array[]` | `std::thread Thread_array[]` | service.h:100 |
| 7 | `void* General_Thread(void*)` | `void General_Thread(Thread_Pool*)` | service.h:111, service.cpp:330 |
| 8 | `pthread_mutex_lock/unlock` | `std::lock_guard`（RAII 自动释放） | service.cpp:278, 475, 736 |
| 9 | `pthread_mutex_lock/unlock` | `std::unique_lock`（配合 wait） | service.cpp:315, 343 |
| 10 | `pthread_cond_wait` + while 循环 | `cond.wait(lock, predicate)` | service.cpp:346 |
| 11 | `pthread_cond_signal` | `cond.notify_one()` | service.cpp:326 |
| 12 | `pthread_cond_broadcast` | `cond.notify_all()` | service.cpp:281 |
| 13 | `pthread_create` | `std::thread(...) move assign` | service.cpp:337 |
| 14 | `pthread_join` | `Thread_array[i].join()` + `joinable()` 检查 | service.cpp:284 |
| 15 | `malloc(sizeof(task_t)*n)` | `new task_t[n]()` 值初始化 | service.cpp:292 |
| 16 | `free(dequeue)` | `delete[] dequeue` | service.cpp:286 |
| 17 | `delete C_P_array` | `delete[] C_P_array` | service.cpp:811 |
| 18 | `pthread_mutex_init` | 移除（默认构造） | service.cpp:305, 759 |
| 19 | `pthread_cond_init` | 移除（默认构造） | service.cpp:305 |
| 20 | `pthread_mutex_destroy` | 移除（析构自动） | service.cpp:287, 812 |
| 21 | `pthread_cond_destroy` | 移除（析构自动） | service.cpp:287 |
| 22 | `#include <pthread.h>`（cpp 内） | 删除 | service.cpp:10 |

---

## 八、验证结果

```
g++ -std=c++11 -g -O0 -o service service.cpp -lpthread -lmysqlclient -ljsoncpp   ✓
g++ -std=c++11 -g -O0 -o client client.cpp -lpthread -lmysqlclient -ljsoncpp    ✓
```

- `service` + `client` 产物均生成 ✓
- 无 `pthread` 函数调用残留（仅注释说明）✓
- 无 `malloc`/`free` 调用残留（仅注释）✓
- `std::thread`/`mutex`/`condition_variable`/`lock_guard`/`unique_lock` 全部就位 ✓

---

## 九、启动方式

```bash
cd /home/xiaoc/mycode/cs++view
./start_all.sh    # 启动 C++(9527) + Web(5500) + 开浏览器
./stop_all.sh     # 停止（PID 文件方式，不误杀其他项目）
```

浏览器访问：`http://localhost:5500`
