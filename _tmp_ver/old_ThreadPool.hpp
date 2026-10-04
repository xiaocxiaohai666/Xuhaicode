#ifndef FIXEDTHREADPOOL_HPP
#define FIXEDTHREADPOOL_HPP

#include "TaskQueue.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

// 线程函数能访问什么取决于这个可调用对象本身的作用域/捕获/接收的指针，而不是取决于线程跑起来之后运行的线程作用域。

namespace ctx {
class FixedThreadPool {
private:
  std::list<std::shared_ptr<std::thread>> m_threadsgroup;
  ctx::Taskdeque<Task> m_taskqueue;
  std::atomic<bool> m_running;
  std::once_flag m_flag; // 意义是确保工作线程只被创建一次，并且是线程安全的。
  int thread_num;
  int deque_size;

  void Start(int thread_num) {
    this->thread_num = thread_num;
    m_running = true;

    std::call_once(m_flag, [this, thread_num] {
      for (int i = 0; i < thread_num; i++) {
        m_threadsgroup.emplace_back(
            std::make_shared<std::thread>(&FixedThreadPool::RuninThread, this));
        // 共享指针这样传参数，完成了线程初始化以及赋值两个步骤：
        // 第一步创建线程，将小括号内的两个参数传递给thread线程类进行初始化，初始化成功即成功创建了一个线程。
        // std::thread(&FixedThreadPool::RuninThread, this);这是thread的初始化。
        // 第二步，将这个线程交给这个共享指针进行管理。

        // 所以参数这么写的原因，是因为线程要执行的函数入口，是一个类里面的函数指针，我们要让这个线程知道这个入口在哪里，即当前类对象的首地址，加上偏移量
        // 所以我们要传入当前线程池的地址加入它的偏移量，注意参数位置互换一下。
      }
    });
  }

  void RuninThread() {
    while (m_running) {
      Task task;
      if (!m_taskqueue.Take(&task)) {
        break; // 设计的时候能失败只能说明任务队列未被启用，所以只能退出线程，因为无法从任务队列取出任务。
      }
      task();
    }
  }

  void ForceStopThreadGroup() {
    m_running = false;
    m_taskqueue.Stop();
    for (auto it = m_threadsgroup.begin(); it != m_threadsgroup.end(); it++) {
      if ((*it)->joinable()) {
        (*it)->join();
      }
    }
  }

public:
  FixedThreadPool(int deque_size,
                  int numthread = std::thread::hardware_concurrency())
      : m_taskqueue(deque_size), deque_size(deque_size) {
    Start(numthread);
  }

  ~FixedThreadPool() { Stop(); }

  void Stop() { ForceStopThreadGroup(); }

  void AddTask(const Task &task) {
    if (!m_taskqueue.Put(task)) {
      std::cout << "AddTask(const Task&) false" << std::endl;
    }
  }

  void AddTask(Task &&task) {
    if (!m_taskqueue.Put(std::forward<Task &&>(task))) {
      std::cout << "AddTask(Task&&) false" << std::endl;
    }
  }
};
} // namespace ctx

namespace ctx {

#define MINITHREADNUM 1
#define TIMEOUT 60

struct Worker {
  std::thread thread;
  std::atomic<bool> busy{false};

  Worker() = default; // 表示使用编译器原生的构造函数
  // 效果类似于Worker() : thread(), busy(false) {}
};

class CacheThreadPool {
private:
  std::list<std::shared_ptr<Worker>> m_threadsgroup;

  ctx::Taskdeque<Task> m_taskqueue;
  std::atomic<bool> m_running;
  // a开头的是管理空闲线程id
  std::mutex a_mutex;
  std::queue<std::thread::id> m_exit_threads_id;
  // b开头管理的是线程数组
  std::mutex b_mutex;

  // 管理线程
  std::thread sudo;

  int minithread_num;
  int maxthread_num;
  int deque_size;
  std::atomic<int> thread_num_now{0}; // 当前创建的子线程数
  std::chrono::milliseconds
      timeout; // 超时相当于设置了一段闹钟，过了一段时间就会让线程从wait的队列中醒来

  void Start(int thread_num) {
    this->maxthread_num = thread_num;
    m_running = true;

    for (int i = 0; i < thread_num_now; i++) {
      auto worker = std::make_shared<Worker>();
      Worker *self =
          worker.get(); // 知道自己的位置在哪里并传递给线程函数好让他改变状态。

      worker->thread = std::thread(&CacheThreadPool::RuninThread, this, self);
      {
        std::lock_guard<std::mutex> lock(b_mutex);
        m_threadsgroup.emplace_back(std::move(worker));
      }
    }

    this->sudo = std::thread(&CacheThreadPool::Sudo, this);
  }

  void RuninThread(Worker *self) {
    while (m_running) {
      Task task;
      int result = m_taskqueue.Cache_Take(&task, timeout);

      if (result == 1) {
        self->busy = true;
        task();
        self->busy = false;
      } else if (result == 0) {
        self->busy = false;
        {
          std::lock_guard<std::mutex> lock(a_mutex);
          m_exit_threads_id.push(std::this_thread::get_id());
        }
        return;
      } else {
        return;
      }
    }
  }

  void AddThread() {
    if (thread_num_now >= maxthread_num || m_running == false) {
      return;
    }

    {
      std::unique_lock<std::mutex> locker(b_mutex);
      ++thread_num_now;

      auto worker = std::make_shared<Worker>();
      Worker *self = worker.get();

      worker->thread = std::thread(&CacheThreadPool::RuninThread, this, self);

      m_threadsgroup.emplace_back(std::move(worker));
    }
  }

  bool CleanupThread() {
    std::thread::id exit_id;
    {
      std::lock_guard<std::mutex> lock(a_mutex);
      if (m_exit_threads_id.empty()) {
        return false;
      }
      exit_id = m_exit_threads_id.front();
      m_exit_threads_id.pop();
    }

    std::shared_ptr<Worker> target;
    {
      std::lock_guard<std::mutex> lock(b_mutex);
      for (const auto &worker : m_threadsgroup) {
        if (worker->thread.get_id() == exit_id) {
          target = worker;
          break;
        }
      }
    }

    if (!target) {
      return false;
    }

    if (target->thread.joinable()) {
      target->thread.join();
    }

    {
      std::lock_guard<std::mutex> lock(b_mutex);
      for (auto it = m_threadsgroup.begin(); it != m_threadsgroup.end(); ++it) {
        if (*it == target) {
          m_threadsgroup.erase(it);
          --thread_num_now;
          break;
        }
      }
    }
    return true;
  }

  void Sudo() { // 但是线程有空闲的情况属于正常情况
    while (m_running) {
      if (CleanupThread()) {
        continue;
      }
      if (!m_taskqueue.Empty()) {
        AddThread();
      } else {
        std::this_thread::
            yield(); // 当前线程主动让出CPU使用权，提示操作系统可以先运行其他线程，什么时候调用它取决于操作系统
        // 是空循环的一种优化方案。
      }
    }
  }

  void ForceStopThreadGroup() {

    m_running = false;
    m_taskqueue.Stop();
    std::unique_lock<std::mutex> locker(b_mutex);
    for (auto it = m_threadsgroup.begin(); it != m_threadsgroup.end();) {
      if ((*it)->thread.joinable()) {
        (*it)->thread.join();
        --thread_num_now;
        it = m_threadsgroup.erase(
            it); // erase本身会返回删除元素后的下一个迭代器。
      }
    }
  }

public:
  CacheThreadPool(int deque_size, int thread_num_now = MINITHREADNUM,
                  int minithread_num = MINITHREADNUM,
                  int maxnumthread = std::thread::hardware_concurrency())
      : m_taskqueue(deque_size), deque_size(deque_size),
        thread_num_now(thread_num_now), minithread_num(minithread_num) {
    timeout = std::chrono::milliseconds(TIMEOUT);
    Start(maxnumthread);
  }

  ~CacheThreadPool() {
    Stop();
    if (sudo.joinable()) {
      sudo.join();
    }
  }

  void Stop() { ForceStopThreadGroup(); }

  void AddTask(const Task &task) {
    if (!m_taskqueue.Put(task)) {
      std::cout << "AddTask(const Task&) false" << std::endl;
    }
  }

  void AddTask(Task &&task) {
    if (!m_taskqueue.Put(std::forward<Task &&>(task))) {
      std::cout << "AddTask(Task&&) false" << std::endl;
    }
  }
};

} // namespace ctx

namespace ctx {
class SignalThreadPool {
private:
  ctx::Taskdeque<Task> m_taskqueue;
  std::atomic<bool> m_running{true};
  std::thread work;

  int deque_size;

  void RuninThread() {
    while (m_running) {
      Task task;
      if (!m_taskqueue.Take(&task)) {
        break;
      }
      task();
    }
  }

  void ForceStopThreadGroup() {
    m_running = false;
    m_taskqueue.Stop();
    if (work.joinable()) {
      work.join();
    }
  }

public:
  SignalThreadPool(int deque_size)
      : deque_size(deque_size), m_taskqueue(deque_size) {
    this->work = std::thread(&SignalThreadPool::RuninThread, this);
  }

  ~SignalThreadPool() { Stop(); }

  void Stop() { ForceStopThreadGroup(); }

  void AddTask(const Task &task) {
    if (!m_taskqueue.Put(task)) {
      std::cout << "AddTask(const Task&) false" << std::endl;
    }
  }

  void AddTask(Task &&task) {
    if (!m_taskqueue.Put(std::forward<Task &&>(task))) {
      std::cout << "AddTask(Task&&) false" << std::endl;
    }
  }
};

} // namespace ctx

namespace ctx {

class
    ScheduledThreadPool { // 设计思路就是，除了取任务的时候可能会休眠，像其他的添加任务，执行任务，以及创建销毁线程都是立即执行，不受任何的影响。
private:
  std::list<std::shared_ptr<Worker>> m_threadsgroup;

  ctx::PriorityTaskqueue task_p_q;

  std::atomic<bool> m_running;
  // a开头的是管理空闲线程id
  std::mutex a_mutex;
  std::queue<std::thread::id> m_exit_threads_id;
  // b开头管理的是线程数组
  std::mutex b_mutex;

  // 管理线程
  std::thread sudo;

  int minithread_num;
  int maxthread_num;
  int deque_size;
  std::atomic<int> thread_num_now{0}; // 当前创建的子线程数
  std::chrono::milliseconds
      timeout; // 超时相当于设置了一段闹钟，过了一段时间就会让线程从wait的队列中醒来

  void Start(int thread_num) {
    this->maxthread_num = thread_num;
    m_running = true;

    for (int i = 0; i < thread_num_now; i++) {
      auto worker = std::make_shared<Worker>();
      Worker *self =
          worker.get(); // 知道自己的位置在哪里并传递给线程函数好让他改变状态。

      worker->thread =
          std::thread(&ScheduledThreadPool::RuninThread, this, self);
      {
        std::lock_guard<std::mutex> lock(b_mutex);
        m_threadsgroup.emplace_back(std::move(worker));
      }
    }

    this->sudo = std::thread(&ScheduledThreadPool::Sudo, this);
  }

  void RuninThread(Worker *self) {
    while (m_running) {
      ScheduledTask task;
      int result = task_p_q.Cache_Take(&task, timeout);

      if (result == 1) {
        self->busy = true;
        task.task();
        self->busy = false;
      } else if (result == 0) {
        self->busy = false;
        {
          std::lock_guard<std::mutex> lock(a_mutex);
          m_exit_threads_id.push(std::this_thread::get_id());
        }
        return;
      } else {
        return;
      }
    }
  }

  void AddThread() {
    if (thread_num_now >= maxthread_num || m_running == false) {
      return;
    }

    {
      std::unique_lock<std::mutex> locker(b_mutex);
      ++thread_num_now;

      auto worker = std::make_shared<Worker>();
      Worker *self = worker.get();

      worker->thread =
          std::thread(&ScheduledThreadPool::RuninThread, this, self);

      m_threadsgroup.emplace_back(std::move(worker));
    }
  }

  bool CleanupThread() {
    std::thread::id exit_id;
    {
      std::lock_guard<std::mutex> lock(a_mutex);
      if (m_exit_threads_id.empty()) {
        return false;
      }
      exit_id = m_exit_threads_id.front();
      m_exit_threads_id.pop();
    }

    std::shared_ptr<Worker> target;
    {
      std::lock_guard<std::mutex> lock(b_mutex);
      for (const auto &worker : m_threadsgroup) {
        if (worker->thread.get_id() == exit_id) {
          target = worker;
          break;
        }
      }
    }

    if (!target) {
      return false;
    }

    if (target->thread.joinable()) {
      target->thread.join();
    }

    {
      std::lock_guard<std::mutex> lock(b_mutex);
      for (auto it = m_threadsgroup.begin(); it != m_threadsgroup.end(); ++it) {
        if (*it == target) {
          m_threadsgroup.erase(it);
          --thread_num_now;
          break;
        }
      }
    }
    return true;
  }

  void Sudo() { // 但是线程有空闲的情况属于正常情况
    while (m_running) {
      if (CleanupThread()) {
        continue;
      }
      if (!task_p_q.Empty()) {
        AddThread();
      } else {
        std::this_thread::
            yield(); // 当前线程主动让出CPU使用权，提示操作系统可以先运行其他线程，什么时候调用它取决于操作系统
        // 是空循环的一种优化方案。
      }
    }
  }

  void ForceStopThreadGroup() {

    m_running = false;
    task_p_q.Stop();
    std::unique_lock<std::mutex> locker(b_mutex);
    for (auto it = m_threadsgroup.begin(); it != m_threadsgroup.end();) {
      if ((*it)->thread.joinable()) {
        (*it)->thread.join();
        --thread_num_now;
        it = m_threadsgroup.erase(
            it); // erase本身会返回删除元素后的下一个迭代器。
      }
    }
  }

public:
  ScheduledThreadPool(int deque_size, int thread_num_now = MINITHREADNUM,
                      int minithread_num = MINITHREADNUM,
                      int maxnumthread = std::thread::hardware_concurrency())
      : deque_size(deque_size), thread_num_now(thread_num_now),
        task_p_q(deque_size), minithread_num(minithread_num) {
    timeout = std::chrono::milliseconds(TIMEOUT);
    Start(maxnumthread);
  }

  ~ScheduledThreadPool() {
    Stop();
    if (sudo.joinable()) {
      sudo.join();
    }
  }

  void Stop() { ForceStopThreadGroup(); }

  void AddTask(const Task &task, std::chrono::milliseconds delay) {
    struct ScheduledTask a;
    a.execute_time = std::chrono::steady_clock::now() +
                     delay; // 获取当前机器时间加上延迟时间
    a.task = std::move(task);
    if (!task_p_q.Put(a)) {
      std::cout << "AddTask(const ScheduledTask&) false" << std::endl;
    }
  }

  void AddTask(Task &&task, std::chrono::milliseconds delay) {
    struct ScheduledTask a;
    a.execute_time = std::chrono::steady_clock::now() +
                     delay; // 获取当前机器时间加上延迟时间
    a.task = std::move(task);
    if (!task_p_q.Put(std::forward<ScheduledTask &&>(a))) {
      std::cout << "AddTask(ScheduledTask&&) false" << std::endl;
    }
  }

  void AddTask(const ScheduledTask &task) {
    if (!task_p_q.Put(task)) {
      std::cout << "AddTask(const ScheduledTask&) false" << std::endl;
    }
  }

  void AddTask(ScheduledTask &&task) {
    if (!task_p_q.Put(std::forward<ScheduledTask &&>(task))) {
      std::cout << "AddTask(ScheduledTask&&) false" << std::endl;
    }
  }
};
} // namespace ctx

#endif

namespace ctx {

class WorkStealingThreadPool {
private:
  std::vector<std::shared_ptr<workers>>
      m_threadsgroup; // 这里改用vector方便下标访问
  std::atomic<bool> m_running;
  std::once_flag m_flag; // 意义是确保工作线程只被创建一次，并且是线程安全的。
  // std::mutex m_mutex;//只有主线程在访问线程数组，可以不用加锁

  int thread_num;
  int deque_size;
  std::atomic<int> index{0};

  void Start(int thread_num) {
    this->thread_num = thread_num;
    m_running = true;

    std::call_once(m_flag, [this, thread_num] {
      m_threadsgroup.reserve(thread_num); // 预分配，避免扩容
      for (int i = 0; i < thread_num; i++) {
        auto tmp = std::make_shared<workers>();
        tmp->Taskdeque_address.reset(new ctx::Taskdeque<Task>(deque_size));
        m_threadsgroup.emplace_back(tmp);
      }//一定要填充好队列之后再去启动线程，因为在扩容的时候，可能队列的起始位置会发生改变，再加上我们自己设计的时候又要将这个队列传进去，如果我们一边启动线程一边扩容
      //可能会出现加入队列的起始位置发生了变化，线程启动的时候又回去遍历一遍线程数组判断是不是初始状态的时候，就遍历了失效地址，所以一定分配好之后，最后确定了这个队列的最终位置后才能传参进去启动线程。

      for (int i = 0; i < thread_num; i++) {
        m_threadsgroup[i]->worker_thread =
            std::thread(&WorkStealingThreadPool::RuninThread, this,
                        m_threadsgroup[i], &m_threadsgroup);
      }
    });


  }

  void RuninThread(std::shared_ptr<workers> a,
                   std::vector<std::shared_ptr<workers>> *b) {
    while (m_running) {
      Task task;
      if (!a->Taskdeque_address->Stealling_Take(&task, b)) {
        continue;
      }
      task();
    }
  }

  void ForceStopThreadGroup() {
    m_running = false;

    for (auto it = m_threadsgroup.begin(); it != m_threadsgroup.end(); it++) {
      (*it)->Taskdeque_address->Stop();
      if ((*it)->worker_thread.joinable()) {
        (*it)->worker_thread.join();
      }
    }
  }

public:
  WorkStealingThreadPool(int deque_size,
                         int numthread = std::thread::hardware_concurrency())
      : deque_size(deque_size) {
    Start(numthread);
  }

  ~WorkStealingThreadPool() { Stop(); }

  void Stop() { ForceStopThreadGroup(); }

  void AddTask(const Task &task) {
    size_t i = index.fetch_add(1, std::memory_order_relaxed) % thread_num;
    auto tmp = m_threadsgroup[i];
    if (!tmp->Taskdeque_address->Put(task)) {
      std::cout << i << " thread add false" << std::endl;
      return;
    }
  }

  void AddTask(Task &&task) {
    size_t i = index.fetch_add(1, std::memory_order_relaxed) % thread_num;
    auto tmp = m_threadsgroup[i];
    if (!tmp->Taskdeque_address->Put(std::forward<Task &&>(task))) {
      std::cout << i << " thread add false" << std::endl;
      return;
    }
  }
};

} // namespace ctx