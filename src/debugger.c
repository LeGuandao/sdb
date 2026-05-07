#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include "debugger.h"
#include "breakpoint.h"
#include "ui.h"

/* ── target (child) ─────────────────────────────────────────────────── */

void run_target(const char *program) {
    printf("[+] 目标程序开始运行: %s\n", program);

    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
        perror("ptrace TRACEME");
        exit(1);
    }
    execl(program, program, NULL);
    perror("execl 失败");
    exit(1);
}

/* ── wait helper ────────────────────────────────────────────────────── */

static int debugger_wait(debugger_state_t *state) {
    int wait_status;
    wait(&wait_status);

    if (WIFEXITED(wait_status)) {
        state->running = 0;
        printf("[+] 子进程已退出，状态码: %d\n", WEXITSTATUS(wait_status));
        return 0;
    }

    if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGTRAP) {
        struct user_regs_struct regs;
        ptrace(PTRACE_GETREGS, state->child_pid, NULL, &regs);

        breakpoint_t *bp = bp_find_by_addr(state, regs.rip - 1);
        if (bp) {
            printf("[!] 命中断点 %d  RIP: 0x%llx\n", bp->id, regs.rip);
            regs.rip = bp->addr;
            ptrace(PTRACE_SETREGS, state->child_pid, NULL, &regs);
            bp_restore_and_step(state, bp);
        }
    }

    state->step_count++;
    return 1;
}

/* ── command handlers ───────────────────────────────────────────────── */

static void cmd_registers(debugger_state_t *state){
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS,state->child_pid,NULL,&regs);

    printf("RAX: 0x%016llx  RBX: 0x%016llx\n", regs.rax, regs.rbx);
    printf("RCX: 0x%016llx  RDX: 0x%016llx\n", regs.rcx, regs.rdx);
    printf("RSI: 0x%016llx  RDI: 0x%016llx\n", regs.rsi, regs.rdi);
    printf("RBP: 0x%016llx  RSP: 0x%016llx\n", regs.rbp, regs.rsp);
    printf("R8 : 0x%016llx  R9 : 0x%016llx\n", regs.r8,  regs.r9);
    printf("R10: 0x%016llx  R11: 0x%016llx\n", regs.r10, regs.r11);
    printf("R12: 0x%016llx  R13: 0x%016llx\n", regs.r12, regs.r13);
    printf("R14: 0x%016llx  R15: 0x%016llx\n", regs.r14, regs.r15);
    printf("RIP: 0x%016llx  RFLAGS: 0x%016llx\n", regs.rip, regs.eflags);
}

static void cmd_memory(debugger_state_t *state){
    char *line = readline("[起始地址]> ");
    if(line == NULL)return;

    uintptr_t addr;

    if(sscanf(line,"%lx",&addr)!=1){
        printf("无效地址\n");
        free(line);
        return;
    }
    free(line);

    for(int row=0;row<10;row++){
        
    }
}

static void cmd_step(debugger_state_t *state) {
    if (ptrace(PTRACE_SINGLESTEP, state->child_pid, NULL, NULL) < 0) {
        perror("单步执行失败");
        return;
    }
    debugger_wait(state);
}

static void cmd_continue(debugger_state_t *state) {
    ptrace(PTRACE_CONT, state->child_pid, NULL, NULL);
    debugger_wait(state);
}

static void cmd_break(debugger_state_t *state) {
    char *line = readline("[输入断点地址]> ");
    if (line == NULL) return;
    uintptr_t addr;
    if (sscanf(line, "%lx", &addr) != 1) {
        printf("[!] 无效地址\n");
    } else {
        int id = bp_set(state, addr);
        /* 根据命令表的说明，b 应该在设置断点后继续运行。仅在设置成功时继续。 */
        if (id > 0) {
            ptrace(PTRACE_CONT, state->child_pid, NULL, NULL);
            debugger_wait(state);
        }
    }
    free(line);
}

static void cmd_info(debugger_state_t *state) {
    bp_list(state);
}

static void cmd_delete(debugger_state_t *state) {
    char *line = readline("[输入断点 ID]> ");
    if (line == NULL) return;
    int id;
    if (sscanf(line, "%d", &id) != 1) {
        printf("[!] 无效 ID\n");
    } else {
        bp_del(state, id);
    }
    free(line);
}

static void cmd_quit(debugger_state_t *state) {
    state->running = 0;
}

static void cmd_help(debugger_state_t *state);

/* ── command table ──────────────────────────────────────────────────── */

static command_t commands[] = {
    {"s",    "单步执行",            cmd_step},
    {"c",    "全速继续",            cmd_continue},
    {"b",    "设置断点并继续运行",   cmd_break},
    {"l",    "列出所有断点",         cmd_info},
    {"d",    "删除断点",            cmd_delete},
    {"help", "显示帮助",            cmd_help},
    {"q",    "退出调试器",          cmd_quit},
    {"r",    "查看寄存器",          cmd_registers},
    {NULL, NULL, NULL}  //哨兵
};

static void cmd_help(debugger_state_t *state) {
    (void)state;
    ui_print_help(commands);
}

/* ── debugger lifecycle ─────────────────────────────────────────────── */

void debugger_init(debugger_state_t *state) {
    int wait_status;
    wait(&wait_status);
    printf("[+] 调试器启动，监听进程: %d\n", state->child_pid);
}

void debugger_run(debugger_state_t *state) {
    while (state->running) {
        char *input = ui_read_command();
        if (input == NULL) {
            state->running = 0;
            break;
        }

        const command_t *cmd = ui_find_command(input, commands);
        if (cmd) {
            free(input);
            cmd->handler(state);
        } else {
            printf("[!] 未知命令: %s (输入 'help' 查看帮助)\n", input);
            free(input);
        }
    }

    printf("[+] 调试完成，子进程 %u 已退出，执行了 %d 步\n",
           state->child_pid, state->step_count);
}
