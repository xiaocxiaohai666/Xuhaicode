add_executable(test test.cpp)

target_compile_options(test PRIVATE -O2 -std=c++17 -Wall)
target_link_libraries(test pthread)
