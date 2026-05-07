# SDB — 简易 Linux 调试器

基于 `ptrace` 的轻量级命令行调试器。

## 功能

- 断点设置 / 删除 / 查看
- 单步执行
- 全速继续运行
- 命令历史（readline）
- 寄存器读取

## 施工区域

- 内存查看
- 符号表解析
- 反汇编?
- PIE?

## 项目结构

```
sdb/
├── include/
│   └── sdb.h              # 共享类型定义（断点、调试器状态、命令表）
├── src/
│   ├── main.c             # 入口：fork 子进程，启动调试器
│   ├── ui.c / ui.h        # 用户交互：readline 输入、命令查找、帮助
│   ├── breakpoint.c / .h  # 断点管理：增删查、ptrace 注入/恢复
│   └── debugger.c / .h    # 核心调试：主循环、命令处理、等待逻辑
├── demo.c                 # 测试用目标程序
├── Makefile
└── README.md
```

## 编译

```bash
make          # 编译 sdb 和 demo
make sdb      # 仅编译调试器
make demo     # 仅编译测试目标（no-pie，地址固定）
make clean    # 清理编译产物
```

## 使用

```bash
./sdb <目标程序>
```

### 命令

| 命令 | 说明 |
|------|------|
| `b`  | 设置断点（交互式输入地址） |
| `c`  | 全速继续执行 |
| `s`  | 单步执行 |
| `l`  | 列出所有断点 |
| `d`  | 删除断点（交互式输入 ID） |
| `q`  | 退出调试器 |
| `help` | 显示帮助 |

### 示例

```bash
# 编译 demo
make demo

# 查看 demo 中 main 的地址
objdump -d demo | grep '<main>'

# 启动调试，在 main 处设置断点
./sdb ./demo
sdb > b
[输入断点地址]> 401126
[+] 断点 1 已设置在 0x401126
sdb > l
ID      地址            状态
1       0x401126        启用
sdb > c
[!] 命中断点 1  RIP: 0x401127
sdb > s
sdb > q
[+] 调试完成，子进程 12345 已退出，执行了 2 步
```

## 添加新命令

专门优化了新功能开发

在 `src/debugger.c` 的 `commands[]` 表中添加一行，并实现对应的处理函数：

```c
static void cmd_myfeature(debugger_state_t *state) {
    // 实现新功能
}

// 在 commands[] 中加入：
{"x", "我的新命令", cmd_myfeature},
```
