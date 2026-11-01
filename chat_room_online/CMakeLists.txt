cmake_minimum_required(VERSION 3.0)
project(chat_room_online)

#导出 compile_commands.json 给 IDE/语言服务器用（它靠这个拿到 include 路径和 C++ 标准，否则满屏红线）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
find_path(MUDUO_INCLUDE_DIR muduo/net/TcpServer.h PATHS /usr/local/include /usr/include NO_DEFAULT_PATH)
find_library(MUDUO_NET_LIB muduo_net PATHS /usr/local/lib NO_DEFAULT_PATH)
find_library(MUDUO_BASE_LIB muduo_base PATHS /usr/local/lib NO_DEFAULT_PATH)
if(NOT MUDUO_INCLUDE_DIR OR NOT MUDUO_NET_LIB OR NOT MUDUO_BASE_LIB)
    message(FATAL_ERROR "未找到 muduo，请确认 /usr/local 下已安装 muduo")
endif()

include_directories(${MUDUO_INCLUDE_DIR})

#C++11 线程库需要显式链接 Threads::Threads
find_package(Threads REQUIRED)

#加载子目录
add_subdirectory(src)
#CMAkE构建的时候会在这个语句下递归跳转到所穿目录名下的CMAKE文件。