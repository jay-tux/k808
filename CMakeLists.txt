cmake_minimum_required(VERSION 3.30)
project(k808 C)

set(CMAKE_C_STANDARD 17)

set(SERVER_PATH "/run/k808.sock" CACHE FILEPATH "Path to the server directory")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DK808_SERVER=\\\"${SERVER_PATH}\\\"")

add_subdirectory(server)
add_subdirectory(cli)