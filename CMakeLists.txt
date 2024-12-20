cmake_minimum_required(VERSION 3.30)
project(k808 C)

set(CMAKE_C_STANDARD 17)

add_executable(k808 main.c
        driver.c)
target_include_directories(k808 PRIVATE /usr/include/libevdev-1.0)
target_link_libraries(k808 evdev pthread)