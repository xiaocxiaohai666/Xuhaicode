cmake_minimum_required(VERSION 3.0)
project(threadpool)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 导出 compile_commands.json，方便 clangd/IDE 识别 include 路径
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message("exepath: " ${EXECUTABLE_OUTPUT_PATH})

include_directories(./include)

add_subdirectory(main)