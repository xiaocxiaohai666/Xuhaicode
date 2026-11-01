#定义一个SRC_LIST 变量，存储了该目录下的所有源文件
#更准确的来说，这是将左边参数目录路径下的所有文件收集给右边的参数变量中，方便下一步进行编译链接
aux_source_directory(. SRC_LIST)
aux_source_directory(./db DB_LIST)
aux_source_directory(./model MODEL_LIST)
aux_source_directory(./redis REDIS_LIST)

#指定生成可执行文件，即定义一个chat_room_server的可执行目标，用后面列出的所有变量展开后的源文件来编译链接。
add_executable(chat_room_server ${SRC_LIST} ${DB_LIST} ${MODEL_LIST} ${REDIS_LIST})

#把配置目录的绝对路径注入成宏 SERVER_CONF_DIR，作为运行时查找配置的默认值；
#启动时传入的 argv[1] 优先级更高，可以覆盖它。
#这是给指定目标追加 -D 宏定义，PRIVATE表示宏只作用于自身的源文件，后面的SERVER_CONF_DIR就是宏名字
target_compile_definitions(chat_room_server PRIVATE SERVER_CONF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/conf")

#指定可执行文件链接时所需要依赖的库文件
target_link_libraries(chat_room_server ${MUDUO_NET_LIB} ${MUDUO_BASE_LIB} mysqlclient hiredis Threads::Threads)
