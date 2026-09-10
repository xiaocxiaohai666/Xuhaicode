# ==============================================================================
# 关于 Makefile 的核心理解
# ==============================================================================

# 1. Makefile文件只负责「编译」和「清理」，不负责其他任何事，也不负责运行可执行文件
#    Makefile文件不只是具有简化长命令的作用，还有会自己判断要不要重新编译指令或者编译文件的顺序的作用。
#    运行程序的事交给 shell 脚本（比如 ser_start.sh）来做，职责分离！

# 2. Makefile 不会「从上到下自动全执行一遍」，它就像一本「快捷指令菜单」：
#    - 冒号前面那个「橙色的东西」= 目标（Target）= 指令名 = 菜单上的菜名
#    - 你必须敲 make + 目标名，才会执行对应命令
#    - 例如：make all / make service / make clean
#    - 如果只敲 make，不写目标名 → 自动执行「菜单第一道菜」，也就是 all

# 3. make all 不会把整个文件跑一遍！
#    只会顺着「依赖链」走：all 依赖 service 和 client → 就做这两个
#    clean 和 all 没关系 → ❌ 完全不碰 clean

# 4. clean 永远不会自动执行！
#    只有你明确敲 make clean，或者脚本里写了 make clean，它才会跑
#    make / make all / make service 全都不会触发 clean

# 5. Make 会「偷懒」（增量编译）：
#    只有当依赖文件（.cpp/.h）比生成的可执行文件「更新」时，才会重新 g++
#    源码没改的话，第二次 make 会直接说：Nothing to be done，别浪费时间

# 6. ⚠️ 最容易踩的坑：冒号下面那条命令（g++、rm）前面必须是 TAB 键！
#    用空格代替 TAB → make 会报错：missing separator

# （.PHONY 已按您的要求移除，日常使用确实不影响；
#   如果哪天目录里真出现名为 clean / all 的文件导致 make clean 失效，再加回来即可）

# ==============================================================================
# 目标定义（菜单）
# ==============================================================================

# all 是菜单的第一道菜，所以「只敲 make」就等于 make all
# all 依赖：先有 service，再有 client → 所以 make all 会编译这两个程序
all:service client
#第一行表示你最终要什么

#接下来写的就是我想要的东西分别是从谁那里得到的，然后是怎么得到的
# service 目标：要得到 service 这个可执行文件，前提是得有 service.cpp
service:service.cpp #从service.cpp得到service
	g++ -o service service.cpp -lpthread -ljsoncpp -lmysqlclient
#需要通过这条语句才能得到这个service可执行文件，也就是怎么得到的
# ↑ 这里开头的空白符是 TAB！不是空格！


client:client.cpp
	g++ -o client client.cpp -lpthread -ljsoncpp -lmysqlclient
# ↑ client.cpp 改了才重新编译 client，没改就跳过

clean:#清理所有临时文件和可执行文件
#   ↑ 这个目标只有在手动敲 make clean 时才会执行
	rm -f *.o service client
#   ↑ 删除 .o 中间文件、service、client 三个东西

# ==============================================================================
# 快速参考（忘了就看这里）
# ==============================================================================
# make            =  make all     → 编译 service + client
# make service                   → 只编译 service
# make client                    → 只编译 client
# make clean                     → 删除编译产物（只有主动敲才执行！）
# make clean && make all         → 先清理再重新编译（全新构建）
# ==============================================================================
