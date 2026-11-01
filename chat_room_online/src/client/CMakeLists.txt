#同样的道理，先定义一个SRC_LIST 变量，存储了该目录下的所有源文件
aux_source_directory(. SRC_LIST)

#指定生成可执行文件
add_executable(chat_room_client ${SRC_LIST})

#把配置目录的绝对路径注入成宏 CLIENT_CONF_DIR，作为运行时查找配置的默认值；
#启动时传入的 argv[1] 优先级更高，可以覆盖它。
#跟服务端的 SERVER_CONF_DIR 是同一套做法
target_compile_definitions(chat_room_client PRIVATE CLIENT_CONF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/conf")

#指定可执行文件链接时所需要依赖的库文件
target_link_libraries(chat_room_client pthread)
