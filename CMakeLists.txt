cmake_minimum_required(VERSION 3.28)
project(untitled18)

set(CMAKE_CXX_STANDARD 17)

add_executable(untitled18 main.cpp
        Thread_Pools.cpp
        Thread_Pools.h)

target_link_libraries(untitled18 ws2_32)
