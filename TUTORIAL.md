# SDB 开发教程 —— 从零构建 Linux 调试器

---

## 目录

1. [调试器原理](#1-调试器原理)
2. [环境准备](#2-环境准备)
3. [Step 1 — fork + ptrace 最小框架](#3-step-1--fork--ptrace-最小框架)
4. [Step 2 — 单步执行与全速继续](#4-step-2--单步执行与全速继续)
5. [Step 3 — 断点注入](#5-step-3--断点注入)
6. [Step 4 — 交互式命令循环](#6-step-4--交互式命令循环)
7. [Step 5 — 模块化重构](#7-step-5--模块化重构)
8. [CMake 构建系统](#8-cmake-构建系统)
9. [常见陷阱与注意事项](#9-常见陷阱与注意事项)
10. [扩展方向](#10-扩展方向)

---

## 1. 调试器原理

### 1.1 ptrace 系统调用

Linux 调试器的核心是 `ptrace`（process trace），它允许一个进程（调试器）控制和检查另一个进程（被调试程序）。

```c
#include <sys/ptrace.h>

long ptrace(enum __ptrace_request request, pid_t pid,
            void *addr, void *data);
```

关键操作：

| 请求 | 作用 |
|------|------|
| `PTRACE_TRACEME` | 子进程声明"我要被调试" |
| `PTRACE_PEEKTEXT` | 读取子进程指定地址的内存 |
| `PTRACE_POKETEXT` | 写入子进程指定地址的内存 |
| `PTRACE_GETREGS` | 读取子进程 CPU 寄存器 |
| `PTRACE_SETREGS` | 修改子进程 CPU 寄存器 |
| `PTRACE_CONT` | 让子进程全速继续执行 |
| `PTRACE_SINGLESTEP` | 让子进程执行一条指令后再次暂停 |

### 1.2 fork-exec-ptrace 模型

```
父进程 (调试器)                    子进程 (被调试程序)
     |                                  |
  fork() ─────────────────────────────> 诞生
     |                                  |
     |                            ptrace(TRACEME)  ← 声明被跟踪
     |                                  |
     |                              execl(program)   ← 加载目标程序
     |                                  |
  wait(&status) <────────────── SIGTRAP (exec 时自动发送)
     |
  ptrace(CONT/SINGLESTEP/...)
     |
  wait(&status) <────────────── 下次停止信号
     |
     ...
```

**关键点**：`execl` 成功后，内核会自动向被跟踪进程发送 `SIGTRAP`，让调试器有机会在目标程序的第一条指令执行前取得控制权。

### 1.3 断点原理（INT3 / 0xCC）

x86 架构上，断点通过 `int3` 指令（机器码 `0xCC`）实现：

1. 读取目标地址的原始指令（8 字节）
2. 将最低字节替换为 `0xCC`
3. 写回目标地址
4. CPU 执行到 `0xCC` 时触发 `SIGTRAP`
5. 调试器收到 `SIGTRAP` 后：
   - RIP 指向 `断点地址+1`（因为 0xCC 占 1 字节）
   - 恢复原始指令
   - 将 RIP 回退到断点地址
   - 单步执行一条指令
   - 重新写入 `0xCC`（让断点持续有效）

```
原始指令:  55 48 89 E5 48 8D 05 C0 ...
写入 0xCC: CC 48 89 E5 48 8D 05 C0 ...
                        ↑ 最低字节被替换
```

---

## 2. 环境准备

```bash
# 确认架构 (必须是 x86_64)
uname -m

# 安装必要的库
sudo pacman -S readline   # Arch Linux
sudo apt install libreadline-dev  # Debian/Ubuntu
```

> **注意**：断点注入依赖 x86 的 `int3` 指令。ARM 架构下断点机制完全不同（使用 `BKPT` 指令），本教程仅适用于 x86_64。

---

## 3. Step 1 — fork + ptrace 最小框架

### 目标

创建一个能用 `ptrace` 启动目标程序并等待其退出的最小调试器。

### 代码

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

void run_target(const char *program) {
    // 1. 声明允许被父进程跟踪
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
        perror("ptrace TRACEME");
        exit(1);
    }
    // 2. 用目标程序替换当前进程
    execl(program, program, NULL);
    // execl 只有失败才会走到这里
    perror("execl 失败");
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("用法: %s <程序路径>\n", argv[0]);
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        run_target(argv[1]);
    } else if (pid > 0) {
        // 父进程
        int status;
        wait(&status);  // 等待子进程的初始 SIGTRAP
        printf("子进程 %d 已就绪\n", pid);

        // 让子进程继续执行
        ptrace(PTRACE_CONT, pid, NULL, NULL);
        wait(&status);  // 等待子进程退出

        if (WIFEXITED(status))
            printf("子进程退出，状态码: %d\n", WEXITSTATUS(status));
    }
    return 0;
}
```

### 知识点

- **`fork()`** 之后，父子进程并发执行，执行顺序不确定。但调试器场景下，子进程调用 `PTRACE_TRACEME` 后会暂停直到父进程调用 `wait()`。
- **`execl`** 会替换当前进程的整个地址空间。必须紧跟 `exit(1)` 处理失败情况，否则子进程会继续执行后续代码。
- **`wait()`** 是阻塞调用，直到子进程状态变化（停止/退出）才返回。

---

## 4. Step 2 — 单步执行与全速继续

在父进程的 `wait` 之后，添加控制逻辑：

```c
// 让子进程单步执行一条指令
ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
wait(&status);

// 让子进程全速继续
ptrace(PTRACE_CONT, pid, NULL, NULL);
wait(&status);
```

### 知识点

- **`PTRACE_SINGLESTEP`** 执行一条指令后，子进程再次暂停并发送 `SIGTRAP`。
- **`PTRACE_CONT`** 全速继续直到下一次信号（断点、单步完成、或程序退出）。
- 每次 `ptrace` 让子进程"跑起来"后，必须再调用 `wait()` 等待它停下。

### WIFSTOPPED / WIFEXITED 宏

```c
wait(&status);

if (WIFEXITED(status)) {
    // 子进程正常退出
    printf("退出码: %d\n", WEXITSTATUS(status));
} else if (WIFSTOPPED(status)) {
    // 子进程因信号暂停
    printf("停止信号: %d\n", WSTOPSIG(status));
}
```

这些宏定义在 `<sys/wait.h>` 中，用于解析 `wait` 返回的状态值。

---

## 5. Step 3 — 断点注入

### 代码

```c
#include <stdint.h>     // uintptr_t
#include <sys/user.h>   // struct user_regs_struct

void set_breakpoint(pid_t pid, uintptr_t addr) {
    // 1. 读取原始指令
    long orig = ptrace(PTRACE_PEEKTEXT, pid, (void *)addr, NULL);

    // 2. 将最低字节替换为 0xCC
    long trap = (orig & ~0xFF) | 0xCC;

    // 3. 写入修改后的指令
    ptrace(PTRACE_POKETEXT, pid, (void *)addr, (void *)trap);
}
```

### 断点命中处理

```c
struct user_regs_struct regs;
ptrace(PTRACE_GETREGS, pid, NULL, &regs);

// 断点命中时 RIP = 断点地址 + 1
if (WSTOPSIG(status) == SIGTRAP && regs.rip == break_addr + 1) {
    printf("命中断点！RIP: 0x%llx\n", regs.rip);

    // 1. RIP 回退到断点地址
    regs.rip = break_addr;
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);

    // 2. 恢复原始指令
    ptrace(PTRACE_POKETEXT, pid, (void *)break_addr, (void *)orig_data);

    // 3. 单步执行（执行原始指令）
    ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
    wait(&status);

    // 4. 重新注入断点
    set_breakpoint(pid, break_addr);
}
```

### 知识点

- **`struct user_regs_struct`** 定义在 `<sys/user.h>`，包含 x86_64 全部寄存器（`rip`, `rax`, `rbx`, `rsp` 等）。
- **`PTRACE_PEEKTEXT` / `PTRACE_POKETEXT`** 以 `long`（8 字节）为单位读写内存。
- **`~0xFF`** 是位掩码，用来清除最低字节；`| 0xCC` 设置最低字节为 int3。
- **为什么 RIP = 断点地址 + 1**：`0xCC` 占 1 字节，CPU 执行后 RIP 指向下一条指令，所以要减 1。

---

## 6. Step 4 — 交互式命令循环

### 使用 GNU Readline

```c
#include <readline/readline.h>
#include <readline/history.h>
#include <string.h>

char *ui_read_command(void) {
    char *input;
    while (1) {
        input = readline("sdb > ");
        if (input == NULL)
            return NULL;       // Ctrl+D
        if (strlen(input) > 0) {
            add_history(input); // 上箭头可回溯
            return input;       // 调用者负责 free(input)
        }
        free(input);            // 空行不记历史
    }
}
```

**关键点**：
- `readline` 分配堆内存，必须由调用者 `free()`。
- 空行不添加到历史记录，否则按上箭头会显示空白。
- 返回 `NULL` 表示 EOF（Ctrl+D），应触发退出。

### 命令分发表

用函数指针表替代 `if-else` 链：

```c
typedef void (*cmd_handler_t)(debugger_state_t *);

typedef struct {
    const char    *name;
    const char    *help;
    cmd_handler_t  handler;
} command_t;

static command_t commands[] = {
    {"s",    "单步执行",  cmd_step},
    {"c",    "继续执行",  cmd_continue},
    {"b",    "设置断点",  cmd_break},
    {"i",    "列出断点",  cmd_info},
    {"d",    "删除断点",  cmd_delete},
    {"help", "显示帮助",  cmd_help},
    {"q",    "退出",      cmd_quit},
    {NULL, NULL, NULL}  // 哨兵
};
```

**为什么这样设计**：
- 添加新命令只需加一行 + 写一个函数
- 命令名、帮助文本、处理函数集中管理
- 循环查找只需遍历表，代码简洁

---

## 7. Step 5 — 模块化重构

### 7.1 拆分原则

| 模块 | 职责 | 不做什么 |
|------|------|----------|
| `main.c` | fork, 状态初始化, 启动调试循环 | 不操作 ptrace |
| `ui.c` | readline 输入, 命令查找, 帮助 | 不知道调试器状态结构 |
| `breakpoint.c` | 断点增删查, ptrace PEEK/POKE | 不处理命令 |
| `debugger.c` | 命令处理, 主循环, wait 逻辑 | 不操作 readline |

### 7.2 头文件设计

```c
// include/sdb.h —— 所有模块共享的类型
#ifndef SDB_H    // ← 防止重复包含
#define SDB_H

#define MAX_BREAKPOINTS 64

typedef struct {
    int       id;
    uintptr_t addr;
    long      orig_data;
    int       enabled;
} breakpoint_t;

typedef struct debugger_state debugger_state_t;  // 前向声明

struct debugger_state {
    pid_t         child_pid;
    breakpoint_t  breakpoints[MAX_BREAKPOINTS];
    int           bp_count;
    int           running;
    int           step_count;
};

#endif
```

### 7.3 编译与链接

```bash
# 分步理解
gcc -c -Iinclude -o src/main.o src/main.c        # 编译
gcc -c -Iinclude -o src/ui.o src/ui.c            # 编译
gcc -c -Iinclude -o src/breakpoint.o src/breakpoint.c
gcc -c -Iinclude -o src/debugger.o src/debugger.c
gcc -o sdb src/*.o -lreadline                     # 链接

# Makefile 自动化
make && ./sdb ./demo
```

**`-Iinclude`** 告诉编译器去 `include/` 目录找头文件。
**`-lreadline`** 链接 libreadline 动态库。

### 7.4 include 顺序问题（血泪教训）

**错误示范**：
```c
#include <readline/readline.h>  // ❌ readline 依赖 FILE
#include <stdio.h>              // 晚了！readline 已经报错
```

**正确示范**：
```c
#include <stdio.h>              // ✅ 先引入依赖
#include <readline/readline.h>  // 再引入 readline
```

**原因**：readline 头文件使用了 `FILE *` 类型，而 `FILE` 定义在 `<stdio.h>` 中。C 的 `#include` 是文本替换，顺序很重要。

### 7.5 不要混用 scanf 和 readline

```c
// ❌ 错误：scanf 会缓冲 stdin，导致 readline 读到空
scanf("%lx", &addr);        // 消耗了 stdin 缓冲
input = readline("> ");     // 可能返回 NULL（管道场景）

// ✅ 正确：统一使用 readline + sscanf
char *line = readline("[地址]> ");
sscanf(line, "%lx", &addr);
free(line);
```

---

## 8. CMake 构建系统

### 8.1 为何用 CMake

Makefile 在简单项目中足够，但 CMake 提供了：
- 跨平台支持（Linux/macOS/Windows）
- 自动检测依赖
- 更好的 IDE 集成（CLion, VS Code CMake Tools）
- `out-of-source` 构建（不污染源码目录）

### 8.2 CMakeLists.txt

在项目根目录创建：

```cmake
cmake_minimum_required(VERSION 3.16)
project(sdb VERSION 1.0.0 LANGUAGES C)

# C11 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 严格警告
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# 头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 查找 readline 库
find_library(READLINE_LIB readline REQUIRED)
if(NOT READLINE_LIB)
    message(FATAL_ERROR "需要安装 libreadline-dev")
endif()

# 调试器可执行文件
add_executable(sdb
    src/main.c
    src/ui.c
    src/breakpoint.c
    src/debugger.c
)
target_link_libraries(sdb PRIVATE ${READLINE_LIB})

# 测试目标程序（no-pie 使地址固定，方便调试）
add_executable(demo demo.c)
target_link_options(demo PRIVATE -no-pie)
```

### 8.3 使用方式

```bash
# 项目根目录下
mkdir build && cd build    # out-of-source 构建
cmake ..                   # 生成 Makefile
make                       # 编译

# 或者一行
cmake -B build && cmake --build build

# 运行
./build/sdb ./build/demo
```

### 8.4 对比 Makefile vs CMake

| 特性 | Makefile | CMake |
|------|----------|-------|
| 手动写依赖 | 需要 | 自动 |
| 跨平台 | 否（依赖 GNU make） | 是 |
| find_library | 手动 | 自动 |
| IDE 支持 | 有限 | 广泛 |
| 学习曲线 | 低 | 中 |
| 适合场景 | 小项目，快速原型 | 多文件、跨平台项目 |

---

## 9. 常见陷阱与注意事项

### 9.1 调试器不能调试自己

`PTRACE_TRACEME` 要求一个进程只能被一个调试器跟踪。如果已经用 gdb 附加到进程上，`PTRACE_TRACEME` 会失败。

### 9.2 PIE 地址随机化

现代 Linux 默认编译 PIE（Position Independent Executable），地址每次运行都不同：

```bash
# 查看是否 PIE
file ./demo
# PIE:  ELF 64-bit LSB pie executable
# 非PIE: ELF 64-bit LSB executable

# 编译非 PIE（测试用，地址固定）
gcc -no-pie -o demo demo.c

# 或者运行时查找实际加载地址
cat /proc/<pid>/maps
```

### 9.3 ptrace 权限

某些 Linux 发行版限制 ptrace 权限（`ptrace_scope`）：

```bash
# 查看当前设置
cat /proc/sys/kernel/yama/ptrace_scope
# 0 — 无限制
# 1 — 仅允许父进程跟踪子进程（默认）
# 2 — 仅允许 root
# 3 — 完全禁止

# 临时放宽（需 root）
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

### 9.4 断点地址对齐

`PTRACE_PEEKTEXT` 以 word 为单位操作。在 x86_64 上 word = 8 字节。如果断点地址不对齐，会读到错误的指令数据。通常函数第一条指令的地址是 16 字节对齐的，所以很少出问题。

### 9.5 内存释放问题

```c
// ❌ 经典错误：返回已释放的指针
char *func() {
    char *p = readline("> ");
    free(p);
    return p;  // use-after-free!
}

// ✅ 正确：调用者负责释放
char *func() {
    char *p = readline("> ");
    if (p && strlen(p) > 0) return p;
    free(p);
    return NULL;
}
```

### 9.6 SIGTRAP 不只来自断点

`SIGTRAP` 除了断点外还有其他来源（如单步完成）。不能仅凭 `SIGTRAP` 判断命中断点，必须同时检查 `RIP == break_addr + 1`。

### 9.7 子进程退出检测

```c
wait(&status);

if (WIFEXITED(status)) {
    // 正常退出 — 调试结束
} else if (WIFSTOPPED(status)) {
    if (WSTOPSIG(status) == SIGTRAP) {
        // 断点或单步 — 继续调试
    }
    // 可能还有其他信号需要处理
}
```

---

## 10. 扩展方向

### 10.1 短期可实现

- **内存查看** — `x/<n>x <addr>` 读取并显示内存
- **寄存器查看** — `info registers` 打印所有寄存器
- **符号解析** — 使用 `libelf` 或 `dwarf.h` 解析函数名
- **条件断点** — 命中时检查寄存器/内存条件
- **断点反汇编** — 显示断点附近的汇编指令（用 `capstone` 库）

### 10.2 中期目标

- **watchpoint** — `PTRACE_PEEKTEXT` 轮询检测内存变化（或用硬件 watchpoint 寄存器）
- **反汇编引擎** — 集成 [Capstone](https://www.capstone-engine.org/) 进行指令解码
- **源码级调试** — 解析 DWARF 调试信息，实现"在 main.c 第 5 行设置断点"
- **远程调试** — gdb 的 `target remote` 协议

### 10.3 学习资源

- `man ptrace` — ptrace 手册页，非常详细
- [Eli Bendersky: How debuggers work](https://eli.thegreenplace.net/2011/01/23/how-debuggers-work-part-1) — 经典三部曲
- [Playing with ptrace](https://www.linuxjournal.com/article/6100) — Linux Journal 文章
- x86_64 ABI 文档 — 理解寄存器约定
