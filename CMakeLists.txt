cmake_minimum_required(VERSION 3.12)
project(MyProject)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)

# 添加可执行文件1
add_executable(client client.cc)

# 添加可执行文件2
add_executable(forwarder forwarder.cc)

# 添加可执行文件3
add_executable(server server.cc histogram/histogram.cc)

target_link_libraries(client PRIVATE ibverbs pthread rdmacm)
target_link_libraries(forwarder PRIVATE ibverbs pthread rdmacm)
target_link_libraries(server PRIVATE ibverbs pthread rdmacm)
