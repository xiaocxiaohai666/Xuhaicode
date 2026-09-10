// 五种线程池抗压测试程序
// 测试对象：FixedThreadPool / CacheThreadPool / SignalThreadPool /
//           ScheduledThreadPool / WorkStealingThreadPool
// 场景：
//   1. LIGHT  轻量任务：100 万任务，多生产者提交，测吞吐 TPS 与排队延迟分布
//   2. CPU    CPU 密集任务：固定计算量，测纯计算吞吐
//   3. SCHED  定时任务：随机延时 50~500ms，测调度时间精度（仅 ScheduledThreadPool）
//   4. BURST  突发流量：3 波任务 + 波间空闲，50ms 采样完成速率，观察扩缩容
//
// 所有结果以 "RESULT|" / "BURST_SAMPLE|" / "ENV|" / "MEM|" 前缀输出，便于提取。

#include "ThreadPool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono;
using ctx::Task;

// ---------------- 工具函数 ----------------

static inline int64_t NowUs() {
  return duration_cast<microseconds>(
             steady_clock::now().time_since_epoch())
      .count();
}

// 统一提交接口：ScheduledThreadPool 的 AddTask 需要 delay 参数，压延时用 0
static void SubmitTask(ctx::FixedThreadPool &p, Task t) {
  p.AddTask(std::move(t));
}
static void SubmitTask(ctx::CacheThreadPool &p, Task t) {
  p.AddTask(std::move(t));
}
static void SubmitTask(ctx::SignalThreadPool &p, Task t) {
  p.AddTask(std::move(t));
}
static void SubmitTask(ctx::WorkStealingThreadPool &p, Task t) {
  p.AddTask(std::move(t));
}
static void SubmitTask(ctx::ScheduledThreadPool &p, Task t) {
  p.AddTask(std::move(t), milliseconds(0));
}

// 固定计算量的 CPU 任务，volatile 防止被编译器优化掉
static void CpuWork(int load) {
  volatile double x = 0.0;
  for (int i = 1; i < load; ++i) {
    x = x + std::sqrt(static_cast<double>(i));
  }
}

static int64_t Pct(std::vector<int64_t> &v, double q) {
  return v[static_cast<size_t>(q * static_cast<double>(v.size() - 1))];
}

static int64_t VmRSSkB() {
  FILE *f = std::fopen("/proc/self/status", "r");
  if (!f) return -1;
  char line[256];
  long rss = -1;
  while (std::fgets(line, sizeof(line), f)) {
    if (std::sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
  }
  std::fclose(f);
  return static_cast<int64_t>(rss);
}

static int64_t PeakRSSkB() {
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  return static_cast<int64_t>(ru.ru_maxrss); // Linux 上单位为 kB
}

// ---------------- 场景 1：轻量任务吞吐 + 延迟 ----------------

template <typename Pool>
void BenchLight(const char *name, Pool &pool, int64_t total, int producers) {
  std::vector<int64_t> lat(static_cast<size_t>(total), 0);
  std::atomic<int64_t> done{0};
  std::atomic<int64_t> remaining{total};
  int64_t per = total / producers;

  int64_t t0 = NowUs();
  std::vector<std::thread> ths;
  for (int p = 0; p < producers; ++p) {
    ths.emplace_back([&, p] {
      int64_t base = static_cast<int64_t>(p) * per;
      int64_t cnt = (p == producers - 1) ? total - base : per;
      for (int64_t i = 0; i < cnt; ++i) {
        int64_t idx = base + i;
        int64_t submit = NowUs();
        SubmitTask(pool, [&lat, &done, &remaining, idx, submit] {
          lat[static_cast<size_t>(idx)] = NowUs() - submit;
          done.fetch_add(1, std::memory_order_relaxed);
          remaining.fetch_sub(1, std::memory_order_release);
        });
      }
    });
  }
  for (auto &t : ths) t.join();
  int64_t t1 = NowUs();
  while (remaining.load(std::memory_order_acquire) != 0) {
    std::this_thread::sleep_for(microseconds(200));
  }
  int64_t t2 = NowUs();

  double wall_s = (t2 - t0) / 1e6;
  double submit_s = (t1 - t0) / 1e6;
  double drain_s = (t2 - t1) / 1e6;
  std::sort(lat.begin(), lat.end());
  int64_t sum = std::accumulate(lat.begin(), lat.end(), 0LL);
  std::printf(
      "RESULT|LIGHT|%s|total=%lld|done=%lld|lost=%lld|wall=%.3fs|submit=%.3fs|"
      "drain=%.3fs|tps=%.0f|lat_avg=%.1fus|p50=%lldus|p95=%lldus|p99=%lldus|"
      "max=%lldus|min=%lldus\n",
      name, (long long)total, (long long)done.load(),
      (long long)(total - done.load()), wall_s, submit_s, drain_s,
      static_cast<double>(total) / wall_s,
      static_cast<double>(sum) / static_cast<double>(total),
      (long long)Pct(lat, 0.50), (long long)Pct(lat, 0.95),
      (long long)Pct(lat, 0.99), (long long)lat.back(), (long long)lat.front());
}

// ---------------- 场景 2：CPU 密集任务吞吐 ----------------

template <typename Pool>
void BenchCpu(const char *name, Pool &pool, int64_t total, int producers,
              int load) {
  std::atomic<int64_t> done{0};
  std::atomic<int64_t> remaining{total};
  int64_t per = total / producers;

  int64_t t0 = NowUs();
  std::vector<std::thread> ths;
  for (int p = 0; p < producers; ++p) {
    ths.emplace_back([&, p] {
      int64_t base = static_cast<int64_t>(p) * per;
      int64_t cnt = (p == producers - 1) ? total - base : per;
      for (int64_t i = 0; i < cnt; ++i) {
        SubmitTask(pool, [&done, &remaining, load] {
          CpuWork(load);
          done.fetch_add(1, std::memory_order_relaxed);
          remaining.fetch_sub(1, std::memory_order_release);
        });
      }
    });
  }
  for (auto &t : ths) t.join();
  int64_t t1 = NowUs();
  while (remaining.load(std::memory_order_acquire) != 0) {
    std::this_thread::sleep_for(microseconds(200));
  }
  int64_t t2 = NowUs();

  double wall_s = (t2 - t0) / 1e6;
  double submit_s = (t1 - t0) / 1e6;
  double drain_s = (t2 - t1) / 1e6;
  std::printf(
      "RESULT|CPU|%s|total=%lld|done=%lld|lost=%lld|wall=%.3fs|submit=%.3fs|"
      "drain=%.3fs|tps=%.0f|load=%dops\n",
      name, (long long)total, (long long)done.load(),
      (long long)(total - done.load()), wall_s, submit_s, drain_s,
      static_cast<double>(total) / wall_s, load);
}

// ---------------- 场景 3：定时任务调度精度（仅 ScheduledThreadPool） ----------------

static void BenchSchedule(ctx::ScheduledThreadPool &pool, int n) {
  struct Rec {
    int64_t expect_run_us; // 预期执行时刻 = 提交时刻 + delay
    int64_t actual_run_us; // 实际执行时刻
  };
  std::vector<Rec> recs(static_cast<size_t>(n));
  std::atomic<int64_t> remaining{n};

  int64_t t0 = NowUs();
  for (int i = 0; i < n; ++i) {
    int64_t delay_ms = 50 + std::rand() % 451; // 50ms ~ 500ms
    int64_t submit = NowUs();
    recs[i].expect_run_us = submit + delay_ms * 1000;
    recs[i].actual_run_us = 0;
    pool.AddTask(
        [&recs, &remaining, i] {
          recs[static_cast<size_t>(i)].actual_run_us = NowUs();
          remaining.fetch_sub(1, std::memory_order_release);
        },
        milliseconds(delay_ms));
  }
  while (remaining.load(std::memory_order_acquire) != 0) {
    std::this_thread::sleep_for(microseconds(200));
  }
  int64_t t1 = NowUs();

  std::vector<int64_t> err(static_cast<size_t>(n));
  int64_t neg = 0; // 提前执行的任务数（理论上不应出现）
  for (int i = 0; i < n; ++i) {
    err[i] = recs[i].actual_run_us - recs[i].expect_run_us;
    if (err[i] < 0) ++neg;
  }
  std::sort(err.begin(), err.end());
  int64_t sum = std::accumulate(err.begin(), err.end(), 0LL);
  std::printf(
      "RESULT|SCHED|ScheduledThreadPool|total=%d|done=%d|wall=%.3fs|"
      "err_avg=%.1fus|p50=%lldus|p95=%lldus|p99=%lldus|max=%lldus|min=%lldus|"
      "early=%lld\n",
      n, n, (t1 - t0) / 1e6, static_cast<double>(sum) / n,
      (long long)Pct(err, 0.50), (long long)Pct(err, 0.95),
      (long long)Pct(err, 0.99), (long long)err.back(), (long long)err.front(),
      (long long)neg);
}

// ---------------- 场景 4：突发流量 + 扩缩容观测 ----------------

template <typename Pool>
void BenchBurst(const char *name, Pool &pool) {
  const int WAVES = 3;
  const int64_t WAVE = 30000;
  const int64_t total = static_cast<int64_t>(WAVES) * WAVE;
  std::atomic<int64_t> done{0};
  std::atomic<int64_t> remaining{total};
  std::atomic<bool> sampling{true};
  std::vector<int64_t> samples;
  samples.reserve(256);

  std::thread sampler([&] {
    while (sampling.load(std::memory_order_relaxed)) {
      samples.push_back(done.load(std::memory_order_relaxed));
      std::this_thread::sleep_for(milliseconds(50));
    }
  });

  int64_t t0 = NowUs();
  for (int w = 0; w < WAVES; ++w) {
    for (int64_t i = 0; i < WAVE; ++i) {
      SubmitTask(pool, [&done, &remaining] {
        done.fetch_add(1, std::memory_order_relaxed);
        remaining.fetch_sub(1, std::memory_order_release);
      });
    }
    std::this_thread::sleep_for(milliseconds(900)); // 波间空闲，等待缩容
  }
  while (remaining.load(std::memory_order_acquire) != 0) {
    std::this_thread::sleep_for(microseconds(200));
  }
  int64_t t1 = NowUs();
  sampling.store(false, std::memory_order_relaxed);
  sampler.join();

  double wall_s = (t1 - t0) / 1e6;
  std::printf("RESULT|BURST|%s|total=%lld|done=%lld|wall=%.3fs|tps_avg=%.0f\n",
              name, (long long)total, (long long)done.load(), wall_s,
              static_cast<double>(total) / wall_s);
  for (size_t i = 1; i < samples.size(); ++i) {
    int64_t d = samples[i] - samples[i - 1];
    std::printf("BURST_SAMPLE|%s|t=%lums|cum_done=%lld|rate=%.0f/s\n", name,
                (long long)(i * 50), (long long)samples[i], d / 0.05);
  }
}

// ---------------- 主流程 ----------------

int main() {
  std::srand(20260909);
  const int hc = static_cast<int>(std::thread::hardware_concurrency());
  const int Q = 200; // 队列容量（Fixed/Cache 内部固定为 MaxTaskCount=200）
  const int64_t LIGHT_TOTAL = 1000000;
  const int64_t CPU_TOTAL = 200000;
  const int CPU_LOAD = 3000; // 每个 CPU 任务 3000 次 sqrt

  std::printf("ENV|hardware_concurrency=%d|nproc=%ld\n", hc,
              ::sysconf(_SC_NPROCESSORS_ONLN));
  std::printf(
      "ENV|light_total=%lld|cpu_total=%lld|cpu_load=%d|queue=%d|producers=%d\n",
      (long long)LIGHT_TOTAL, (long long)CPU_TOTAL, CPU_LOAD, Q, hc);

  {
    ctx::FixedThreadPool pool(Q, hc);
    BenchLight("FixedThreadPool", pool, LIGHT_TOTAL, hc);
    BenchCpu("FixedThreadPool", pool, CPU_TOTAL, hc, CPU_LOAD);
  }
  std::printf("MEM|after=FixedThreadPool|VmRSS=%lldkB|PeakRSS=%lldkB\n",
              (long long)VmRSSkB(), (long long)PeakRSSkB());

  {
    ctx::CacheThreadPool pool(Q); // 默认初始 1 线程，最大 hc 线程
    BenchLight("CacheThreadPool", pool, LIGHT_TOTAL, hc);
    BenchCpu("CacheThreadPool", pool, CPU_TOTAL, hc, CPU_LOAD);
  }
  std::printf("MEM|after=CacheThreadPool|VmRSS=%lldkB|PeakRSS=%lldkB\n",
              (long long)VmRSSkB(), (long long)PeakRSSkB());

  {
    ctx::SignalThreadPool pool(Q); // 单线程
    BenchLight("SignalThreadPool", pool, LIGHT_TOTAL, hc);
    BenchCpu("SignalThreadPool", pool, CPU_TOTAL, hc, CPU_LOAD);
  }
  std::printf("MEM|after=SignalThreadPool|VmRSS=%lldkB|PeakRSS=%lldkB\n",
              (long long)VmRSSkB(), (long long)PeakRSSkB());

  {
    ctx::WorkStealingThreadPool pool(Q, hc);
    BenchLight("WorkStealingThreadPool", pool, LIGHT_TOTAL, hc);
    BenchCpu("WorkStealingThreadPool", pool, CPU_TOTAL, hc, CPU_LOAD);
  }
  std::printf("MEM|after=WorkStealingThreadPool|VmRSS=%lldkB|PeakRSS=%lldkB\n",
              (long long)VmRSSkB(), (long long)PeakRSSkB());

  {
    ctx::ScheduledThreadPool pool(Q);
    BenchLight("ScheduledThreadPool", pool, LIGHT_TOTAL, hc);
    BenchCpu("ScheduledThreadPool", pool, CPU_TOTAL, hc, CPU_LOAD);
    BenchSchedule(pool, 10000);
  }
  std::printf("MEM|after=ScheduledThreadPool|VmRSS=%lldkB|PeakRSS=%lldkB\n",
              (long long)VmRSSkB(), (long long)PeakRSSkB());

  {
    ctx::CacheThreadPool pool(Q);
    BenchBurst("CacheThreadPool", pool);
  }
  {
    ctx::FixedThreadPool pool(Q, hc);
    BenchBurst("FixedThreadPool", pool);
  }

  std::printf("ALL_BENCH_DONE\n");
  return 0;
}
