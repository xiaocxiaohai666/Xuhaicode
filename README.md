# Xuhaicode

本科阶段 **C++ / Linux 网络编程**的项目集合，两个独立项目：一个**并发网络服务器**，一个**线程池库**。

两个项目都配有完整的设计文档、踩坑记录与实测数据；图文版发在 CSDN 博客 [一只旭宝](https://blog.csdn.net/2501_90355051)。

## 目录

| 目录 | 项目 | 一句话 |
|---|---|---|
| [`cs/`](cs/) | **大王山票务预约系统** | C++ 写的 C/S 票务预约系统：TCP 长连接 + epoll + 线程池 + MySQL 连接池，**迭代了四个版本**，最后一版带 Flask 网页界面 |
| [`threadpool/`](threadpool/) | **多线程池库** | header-only 的 **C++17** 线程池，一次实现 **5 种调度策略**（含工作窃取），配完整抗压测试报告 |

## 技术栈对照

| | 票务预约系统 | 线程池库 |
|---|---|---|
| 语言 | C++11（部分早期版本为 C 风格） | C++17 |
| 网络 | TCP + epoll（LT + `EPOLLONESHOT`） | — |
| 并发 | pthread 线程池（5 worker）+ MySQL 连接池 | 5 种池：Fixed / Cache / Signal / Scheduled / WorkStealing |
| 存储 | MySQL（事务 + 连接池） | — |
| 协议 | JSON over TCP（jsoncpp） | — |
| 构建 | Makefile | CMake |
| 可选上层 | Python 3 + Flask 网页界面 | — |
| 实测 | — | **188 万 TPS**（100 万轻量任务，工作窃取池） |

## 想快速了解

| 你想知道 | 看这里 |
|---|---|
| 服务器整体怎么设计的 | [cs/csproject/项目说明.md](cs/csproject/项目说明.md) |
| 踩过哪些坑 | [cs/csproject/项目所遇到的问题.md](cs/csproject/项目所遇到的问题.md) |
| epoll 的原理笔记 | [cs/csproject/epoll笔记.md](cs/csproject/epoll笔记.md) |
| 怎么一步步优化的 | [cs/csproject2/预约系统版本2.md](cs/csproject2/预约系统版本2.md) → [cs/cs++view/C++化改造记录.md](cs/cs++view/C++化改造记录.md) |
| 需求与数据库设计 | [cs/project/需求文档.md](cs/project/需求文档.md) |
| 线程池的设计与源码 | [threadpool/线程池设计文档.md](threadpool/线程池设计文档.md) |
| 线程池的实测数据 | [threadpool/抗压测试报告.md](threadpool/抗压测试报告.md) |

## 环境

Linux（在 Ubuntu 20.04 上实测）。依赖：

```bash
# 票务预约系统
sudo apt install g++ make libjsoncpp-dev libmysqlclient-dev mysql-server

# 线程池
sudo apt install g++ cmake
```

各自的编译与运行方式见两个项目目录下的 README。
