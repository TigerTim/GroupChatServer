cmake_minimum_required(VERSION 3.10)
project(A11_GroupChatServer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")

include_directories(include)

add_executable(server src/server.c)
target_link_libraries(server pthread)

add_executable(client src/client.c)
target_link_libraries(client pthread)