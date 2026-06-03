cmake_minimum_required(VERSION 3.28)
project(untitled18)

set(CMAKE_CXX_STANDARD 17)

set(SQLITE3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite-amalgamation-3460100")

add_library(sqlite3_amalgamation STATIC
    ${SQLITE3_DIR}/sqlite3.c
)
target_include_directories(sqlite3_amalgamation PUBLIC ${SQLITE3_DIR})
if(MSVC)
    target_compile_options(sqlite3_amalgamation PRIVATE /W0)
endif()

add_executable(untitled18
    main.cpp
    Thread_Pools.cpp
    Thread_Pools.h
    MessageStore.cpp
    MessageStore.h
)

target_link_libraries(untitled18 PRIVATE ws2_32 sqlite3_amalgamation)
