#!/bin/bash
# .sh文件是脚本，负责调用 Makefile 编译，并运行我们的可执行程序
# 执行 ./ser_start.sh 后完整流程：
#         │
#         ▼
#   ┌───────────────────────────┐
#   │  ser_start.sh 脚本运行    │
#   │                           │
#   │  ① 检查 Makefile 是否存在 │──不存在──►报错退出
#   │        │                  │
#   │        ▼存在              │
#   │  ② 调用 make clean       │────调用──▶ Makefile 的 clean 目标
#   │        │                  │            │
#   │        │                  │            ▼ rm -rf output *.o
#   │        │                  │
#   │  ③ 调用 make              │────调用──▶ Makefile 的 all 目标
#   │        │                  │            │
#   │        │                  │            ▼ g++ 编译 service.cpp
#   │        │                  │            ▼ g++ 编译 client.cpp
#   │        │                  │            ▼ 生成 output/service
#   │        │                  │
#   │        ▼编译成功          │
#   │  ④ 检查 output/service    │
#   │        │                  │
#   │        ▼                  │
#   │  ⑤ 杀掉旧进程（防端口占用）│
#   │        │                  │
#   │        ▼                  │
#   │  ⑥ ./output/service       │
#   │     service.conf          │◀── 真正的服务程序启动
#   └───────────────────────────┘

# 第一步：检测 Makefile 是否存在，存在就先清理再编译
if [ -f Makefile ]
then
    make clean    # 清理旧的编译产物
    make          # 编译 service 和 client
fi

# 第二步：检测 service 可执行文件是否存在，存在就启动
if [ -f service ]
then
    ./service service.conf
fi
