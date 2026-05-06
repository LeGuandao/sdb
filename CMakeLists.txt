# 最低 CMake 版本要求
cmake_minimum_required(VERSION 3.16)

# 项目信息
project(sdb VERSION 1.0.0 LANGUAGES C)

# C11 标准（for 循环内声明变量需要）
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项：严格警告 + 调试符号
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# 头文件搜索路径，相当于 gcc -Iinclude
include_directories(include)

# 可执行文件 sdb，列出所有 .c 源文件
add_executable(sdb
    src/main.c
    src/ui.c
    src/breakpoint.c
    src/debugger.c
)

# 链接 readline 库，相当于 gcc -lreadline
target_link_libraries(sdb readline)
