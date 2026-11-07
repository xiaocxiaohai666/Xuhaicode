cmake_minimum_required(VERSION 3.0)
project(chat_room_online)

#导出 compile_commands.json 给 IDE/语言服务器用（它靠这个拿到 include 路径和 C++ 标准，否则满屏红线）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#默认开优化：build.sh 是裸跑 cmake 的，CMAKE_BUILD_TYPE 为空时整个项目按 -O0 编译，
#压测里能差好几倍，这里兜一个默认值（-O2 + 调试信息）
if(NOT CMAKE_BUILD_TYPE) #在cmake中的NOT相当于c以及c加加的！取反符号，并且在cmake中变量未定义以及值为空字符串都算假的
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "构建类型" FORCE) #如果这个变量真的未定义或者没有初始值，那么就将这个白色的值进行赋值给这个变量
    #CACHE表示永久缓存CMAKE_BUILD_TYPE这个，STRING表示这个缓存条目的类型，FORCE英文名是强制的意思，表示缓存里已经有值的话进行强行覆盖，这句话存在的意义是防止意外情况，理论上，不会出现仍存有旧缓存值的情况
endif()

#是否启用 AES-256-GCM 加密传输；默认关，保证不改变原有行为
#option是脚本中的bool类型的开关，第一个参数是这个开关的名字，第二个就是文字提示，第三个表示这个默认值是开的。
option(ENABLE_CRYPTO "启用 AES-256-GCM 加密传输" ON)

#配置编译选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

#显式指定 C++ 标准：协议里的结构体用了默认成员初始值 + 聚合初始化，需要 C++14 及以上
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#配置最终的可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)

#配置头文件的搜索路径
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/server)
include_directories(${CMAKE_SOURCE_DIR}/include/server/db)
include_directories(${CMAKE_SOURCE_DIR}/include/server/model)
include_directories(${CMAKE_SOURCE_DIR}/include/server/redis)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty)
link_directories(/usr/lib64/mysql)

#查找 muduo 的头文件与静态库，找不到就直接报错，避免拖到链接阶段才暴露问题
#从做到又一次是自己定义的变量名，你要查找的东西，查找的路径
find_path(MUDUO_INCLUDE_DIR muduo/net/TcpServer.h PATHS /usr/local/include /usr/include NO_DEFAULT_PATH)
find_library(MUDUO_NET_LIB muduo_net PATHS /usr/local/lib NO_DEFAULT_PATH) #NO_DEFAULT_PATH表示不用默认搜索路径
find_library(MUDUO_BASE_LIB muduo_base PATHS /usr/local/lib NO_DEFAULT_PATH)
if(NOT MUDUO_INCLUDE_DIR OR NOT MUDUO_NET_LIB OR NOT MUDUO_BASE_LIB)
    message(FATAL_ERROR "未找到 muduo，请确认 /usr/local 下已安装 muduo")
endif()

include_directories(${MUDUO_INCLUDE_DIR})

#C++11 线程库需要显式链接 Threads::Threads
find_package(Threads REQUIRED)

#压测器是纯 C（用了 pthread_barrier_t 等 POSIX 接口），显式指定 C 标准；
#不写的话 IDE 拿不到正确的编译参数，bench.c 会满屏红线
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

#加载子目录
add_subdirectory(src)
#CMAkE构建的时候会在这个语句下递归跳转到所穿目录名下的CMAKE文件。

#压测器单独一个目标，固定开 -O2
add_executable(bench tools/bench.cpp ${CMAKE_SOURCE_DIR}/src/crypto.cpp)
target_compile_options(bench PRIVATE -O2) #这个选项只给这个bench文件这个用
target_link_libraries(bench pthread)
if(ENABLE_CRYPTO)
    target_compile_definitions(bench PRIVATE ENABLE_CRYPTO)
    target_link_libraries(bench crypto)
endif()
