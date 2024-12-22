cmake_minimum_required(VERSION 3.30)
project(k808-cli C)

set(CMAKE_C_STANDARD 17)

add_executable(k808-cli main.c
        client.c)