#ifndef TASKQUEUE_HPP
#define TASKQUEUE_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace ctx {

struct workers;

static const int MaxTaskCount = 200;
using Task = std::function<void(
    void)>; // cs项目中我们是用c的方法写了一个线程的入口函数，线程是线程，任务是任务，
// 这里我们直接将任务本身当作线程

template <
    class
    T> // 函数模板不仅可以传入变量，用户自定义类型，甚至函数包装器也能够传递进来。
class Taskdeque {
private:
  std::deque<T> m_queue;
  mutable std::mutex m_mutex;
  mutable std::condition_variable m_notEmpty;
  mutable std::condition_variable m_notFull;
  int m_maxSize;
  std::atomic<bool> status;

  bool IsFull() const { return m_queue.size() >= m_maxSize; }

  bool IsEmpty() const { return m_queue.empty(); }

  template <class F> bool Add(F &&task) {
    if (status) {
      std::unique_lock<std::mutex> locker(m_mutex);
      m_notFull.wait(locker,
                     [this]() -> bool { return !IsFull() || !status.load(); });
      if (!status.load()) {
        return false;
      }
      m_queue.push_back(std::forward<F>(task));
      m_notEmpty.notify_all();
      return true;
    }
    std::cout << "task deque status = false" << std::endl;
    return false;
  }

public:
  Taskdeque(int maxsize = MaxTaskCount) : m_maxSize(maxsize), status(true) {}

  ~Taskdeque() { Stop(); }

  Taskdeque(const Taskdeque &) = delete;
  Taskdeque &operator=(const Taskdeque &) = delete;

  bool Put(const T &task) { return Add(task); }

  bool Put(T &&task) { return Add(std::forward<T>(task)); }

  bool Take(T *ptask) {
    if (status) {
      if (nullptr == ptask)
        return false;
      std::unique_lock<std::mutex> locker(m_mutex);
      m_notEmpty.wait(
          locker, [this]() -> bool { return !IsEmpty() || !status.load(); });
      if (m_queue.empty() && !status.load()) {
        return false;
      }
      *ptask = m_queue.front();
      m_queue.pop_front();
      m_notFull.notify_all();
      return true;
    }
    std::cout << "task deque status = false" << std::endl;
    return false;
  }

  // 特殊版本take只针对于工作窃取线程池中的特殊情况
  bool not_cv_Take(T *ptask) {
    if (status) {
      if (nullptr == ptask)
        return false;
      std::unique_lock<std::mutex> locker(m_mutex);

      if (m_queue.empty() || !status.load()) {
        return false;
      }

      *ptask = m_queue.front();
      m_queue.pop_front();
      m_notFull.notify_all();
      return true;
    }
    std::cout << "task deque status = false" << std::endl;
    return false;
  }

  bool Take(std::deque<T> *pdeq) {
    if (status) {
      if (nullptr == pdeq)
        return false;
      std::unique_lock<std::mutex> locker(m_mutex);
      m_notEmpty.wait(
          locker, [this]() -> bool { return !IsEmpty() || !status.load(); });
      *pdeq = std::move(m_queue);
      m_notFull.notify_all();
      return true;
    }
    std::cout << "task deque status = false" << std::endl;
    return false;
  }

  int Cache_Take(T *ptask, std::chrono::milliseconds timeout) {
    if (status) {
      if (nullptr == ptask)
        return -1;

      std::unique_lock<std::mutex> locker(m_mutex);
      bool result = m_notEmpty.wait_for(
          locker, timeout, [this]() -> bool { return !IsEmpty() || !status; });
      if (!status) {
        return -2;
      }

      if (result == false) {

        return 0;
      }

      *ptask = m_queue.front();
      m_queue.pop_front();
      m_notFull.notify_all();
      return 1;
    }

    std::cout << "task deque status = false" << std::endl;
    return -2;
  }

  int Cache_Take(std::deque<T> *pdeq, std::chrono::milliseconds timeout) {
    if (status) {
      if (nullptr == pdeq)
        return -1;
      std::unique_lock<std::mutex> locker(m_mutex);
      bool results = m_notEmpty.wait_for(
          locker, timeout, [this]() -> bool { return !IsEmpty() || !status; });
      if (!status) {
        return -2;
      }

      if (results == false) {
        return 0;
      }

      *pdeq = std::move(m_queue);
      m_notFull.notify_all();
      return 1;
    }
    std::cout << "task deque status = false" << std::endl;
    return -2;
  }

  bool Stealling_Take(T *ptask, std::vector<std::shared_ptr<workers>> *b);

  bool Stealling_Take(std::deque<T> *pdeq) {
    if (status) {
      if (nullptr == pdeq)
        return false;
      std::unique_lock<std::mutex> locker(m_mutex);
      m_notEmpty.wait(
          locker, [this]() -> bool { return !IsEmpty() || !status.load(); });
      *pdeq = std::move(m_queue);
      m_notFull.notify_all();
      return true;
    }
    std::cout << "task deque status = false" << std::endl;
    return false;
  }

  void Stop() {
    {
      std::unique_lock<std::mutex> locker(m_mutex);
      status = false;
    }

    m_notEmpty.notify_all();
    m_notFull.notify_all();
  }

  bool Full() const {
    std::unique_lock<std::mutex> locker(m_mutex);
    return IsFull();
  }

  bool Empty() const {
    std::unique_lock<std::mutex> locker(m_mutex);
    return IsEmpty();
  }

  size_t Size() const {
    std::unique_lock<std::mutex> locker(m_mutex);
    return m_queue.size();
  }

  size_t Count() const { return m_maxSize; }

  bool RE_STATUS() const { return status; };
};

struct workers {
  std::thread worker_thread;
  std::unique_ptr<Taskdeque<Task>> Taskdeque_address;
};

template <class T>
bool Taskdeque<T>::Stealling_Take(T *ptask, std::vector<std::shared_ptr<workers>> *b) {
  if (status) {
    if (nullptr == ptask)
      return false;

    std::unique_lock<std::mutex> locker(m_mutex);
    while (status.load()) {
      // 1. 自己队列非空就直接取，省得再绕去偷别人
      if (!m_queue.empty()) {
        *ptask = std::move(m_queue.front());
        m_queue.pop_front();
        m_notFull.notify_all();
        return true;
      }

      // 2. 释放自己的锁再去偷别人，避免持自己锁去 lock 别人引发 AB-BA 死锁
      locker.unlock();
      for (auto i = (*b).begin(); i != (*b).end(); ++i) {
        if ((*i)->worker_thread.get_id() == std::this_thread::get_id())
          continue; // 跳过自己
        if (!(*i)->Taskdeque_address->RE_STATUS())
          continue;
        T task;
        if ((*i)->Taskdeque_address->not_cv_Take(&task)) {
          // 偷到直接返回，不再 Put 进自己队列——否则会再次 lock m_mutex 自死锁
          *ptask = std::move(task);
          return true;
        }
      }

      // 3. 都没偷到，重新持锁 wait 一小段时间。
      // 用 wait_for 而不是 wait：如果任务被 AddTask 塞到了别人的队列，
      // 唤醒的是别人队列的 m_notEmpty，本线程听不到；超时一段时间后醒来
      // 自己重新遍历去偷，避免错过别人队列里的可偷任务。
      locker.lock();
      m_notEmpty.wait_for(locker, std::chrono::milliseconds(50),
                          [this]() -> bool { return !m_queue.empty() || !status.load(); });
      // 超时或被唤醒后回到 while 顶部：先看自己队列，没有再去偷
    }
  }
  std::cout << "task deque status = false" << std::endl;
  return false;
}

} // namespace ctx

namespace ctx {

#define DELAY 600

struct ScheduledTask {
  Task task;
  std::chrono::steady_clock::time_point execute_time;
};

struct CompareTask {
  bool operator()(const ScheduledTask &a, const ScheduledTask &b) const {
    return a.execute_time > b.execute_time;
  }
};

class PriorityTaskqueue {
private:
  std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, CompareTask>
      m_queue; // 从做到右依次是元素类型，底层容器类型以及比较函数
  mutable std::mutex m_mutex;
  mutable std::condition_variable m_notEmpty;
  mutable std::condition_variable m_notFull;
  int m_maxSize;
  std::atomic<bool> status;

  bool IsFull() const { return m_queue.size() >= m_maxSize; }

  bool IsEmpty() const { return m_queue.empty(); }

  template <class F> bool Add(F &&task) {
    if (status) {
      std::unique_lock<std::mutex> locker(m_mutex);
      m_notFull.wait(locker,
                     [this]() -> bool { return !IsFull() || !status.load(); });
      if (!status.load()) {
        return false;
      }
      m_queue.push(std::forward<F>(task));
      m_notEmpty.notify_all();
      return true;
    }
    std::cout << "task deque status = false" << std::endl;

    return false;
  }

public:
  PriorityTaskqueue(int maxsize = MaxTaskCount)
      : m_maxSize(maxsize), status(true) {}

  ~PriorityTaskqueue() { Stop(); }

  PriorityTaskqueue(const PriorityTaskqueue &) = delete;
  PriorityTaskqueue &operator=(const PriorityTaskqueue &) = delete;

  bool Put(const ScheduledTask &task) { return Add(task); }

  bool Put(ScheduledTask &&task) {
    return Add(std::forward<ScheduledTask>(task));
  }

  int Cache_Take(
      ScheduledTask *ptask,
      std::chrono::milliseconds
          timeout) { // 这里的设计思路是到我们时间才去取这个任务，取完任务立即执行，
    if (nullptr == ptask) {
      return -1;
    }
    while (status) {

      std::unique_lock<std::mutex> locker(m_mutex);
      bool result = m_notEmpty.wait_for(
          locker, timeout, [this]() -> bool { return !IsEmpty() || !status; });
      if (!status) {
        return -2;
      }

      if (result == false) {

        return 0;
      }

      auto time = m_queue.top().execute_time;
      // 因为我们设计线程的逻辑是，一个线程每一次只执行一个任务，所以我们顺利到达或超过时间点的时候取出任务，然后立刻退出这个取任务的函数，
      //  所以能直接取任务的是特殊情况，大部分是需要睡眠一段时间并醒来重新检查的，所以取出任务要单独写一个if语句作为特殊情况的处理

      if (time <= std::chrono::steady_clock::now()) {
        *ptask = m_queue.top();
        m_queue.pop();
        m_notFull.notify_all();
        return 1;
      }

      m_notEmpty.wait_until(
          locker,
          time); // wait_until第二个参数是绝对时间点，到达这一时刻就唤醒线程。
      // 在睡眠的时候会释放锁，但是释放锁的时候可能会有其他线程在加任务，或者去取任务，加任务可能又有更早的任务到达了队头，所以醒来之后重新判断一遍，所以要用while循环
    }

    std::cout << "task priotity_queue status = false" << std::endl;
    return -2;
  }

// 此时可能发生的情况：
#if 0
线程 A 拿到锁，看到队头任务的 execute_time 还没到 → 调 wait_until → 释放锁，睡眠
线程 B 拿到锁（A 释放了），看到同一个队头任务，时间也没到 → 也调 wait_until → 释放锁，睡眠
时间到了 → A、B 同时被唤醒（同一个条件变量 m_notEmpty）
两个线程抢锁，假设 A 先抢到 → 循环回去 → wait_for 发现队列非空 → 看时间到了 → pop 取走任务 → 返回 1
B 抢到锁 → 循环回去 → wait_for 发现队列可能空了 → 如果空了就重新等新任务，如果有别的任务就看那个任务的时间
还有一种可能（队列空不空原理都一样的）：
A、B 都在 wait_until 睡眠，等待同一个任务的 execute_time
时间到了，A、B 被唤醒，尝试重新抢锁
但此时 线程 C 刚执行完上一个任务，调 Cache_Take 也来抢锁
C 先抢到锁 → wait_for 发现队列非空 → 看时间已到 → pop 取走任务 → 返回 1
A 抢到锁 → 循环回去 → wait_for 发现队列空了 → 重新等新任务
B 抢到锁 → 同样，队列空了 → 重新等新任务
中间态的竞争的线程有哪些:
在任务不止一个的条件下：
线程少的时候：所有线程很快都进入 wait_until，时间点到时只有 wait_until 线程被唤醒竞争
线程足够多时：抢锁过程本身消耗的时间不可忽略，execute_time 可能在部分线程还没走完流程时就到达，导致 wait_until 线程、阻塞在 mutex 上的线程、以及 wait_for 中的线程混合竞争
#endif

  int Cache_Take(std::priority_queue<ScheduledTask, std::vector<ScheduledTask>,
                                     CompareTask> *pdeq,
                 std::chrono::milliseconds timeout) {
    if (status) {
      if (nullptr == pdeq)
        return -1;
      std::unique_lock<std::mutex> locker(m_mutex);
      bool results = m_notEmpty.wait_for(
          locker, timeout, [this]() -> bool { return !IsEmpty() || !status; });
      if (!status) {
        return -2;
      }

      if (results == false) {
        return 0;
      }

      *pdeq = std::move(m_queue);
      m_notFull.notify_all();
      return 1;
    }
    std::cout << "task priotity_queue status = false" << std::endl;
    return -2;
  }

  void Stop() {
    {
      std::unique_lock<std::mutex> locker(m_mutex);
      status = false;
    }

    m_notEmpty.notify_all();
    m_notFull.notify_all();
  }

  bool Full() const {
    std::unique_lock<std::mutex> locker(m_mutex);
    return IsFull();
  }

  bool Empty() const {
    std::unique_lock<std::mutex> locker(m_mutex);
    return IsEmpty();
  }

  size_t Size() const {
    std::unique_lock<std::mutex> locker(m_mutex);
    return m_queue.size();
  }

  size_t Count() const { return m_maxSize; }
};

} // namespace ctx
#endif
