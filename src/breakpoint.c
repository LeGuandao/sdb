#include "breakpoint.h"

#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

int bp_set(debugger_state_t *state, uintptr_t addr) {
    if (state->bp_count >= MAX_BREAKPOINTS) {
        printf("[!] 断点数量已达上限\n");
        return -1;
    }

    long orig_data = ptrace(PTRACE_PEEKTEXT, state->child_pid, (void *)addr, NULL);
    long trap_data = (orig_data & ~0xFF) | 0xCC;
    if (ptrace(PTRACE_POKETEXT, state->child_pid, (void *)addr, (void *)trap_data) < 0) {
        perror("ptrace POKETEXT");
        return -1;
    }

    breakpoint_t *bp = &state->breakpoints[state->bp_count++];
    bp->id        = state->bp_count;
    bp->addr      = addr;
    bp->orig_data = orig_data;
    bp->enabled   = 1;

    printf("[+] 断点 %d 已设置在 0x%lx\n", bp->id, addr);
    return bp->id;
}

int bp_del(debugger_state_t *state, int id) {
    for (int i = 0; i < state->bp_count; i++) {
        if (state->breakpoints[i].id == id) {
            breakpoint_t *bp = &state->breakpoints[i];
            ptrace(PTRACE_POKETEXT, state->child_pid, (void *)bp->addr,
                   (void *)bp->orig_data);

            state->breakpoints[i] = state->breakpoints[--state->bp_count];
            printf("[+] 断点 %d 已删除\n", id);
            return 0;
        }
    }
    printf("[!] 未找到断点 %d\n", id);
    return -1;
}

void bp_list(const debugger_state_t *state) {
    if (state->bp_count == 0) {
        printf("[*] 没有断点\n");
        return;
    }
    printf("ID\t地址\t\t状态\n");
    for (int i = 0; i < state->bp_count; i++) {
        const breakpoint_t *bp = &state->breakpoints[i];
        printf("%d\t0x%lx\t%s\n", bp->id, bp->addr,
               bp->enabled ? "启用" : "禁用");
    }
}

breakpoint_t *bp_find_by_addr(debugger_state_t *state, uintptr_t addr) {
    for (int i = 0; i < state->bp_count; i++) {
        if (state->breakpoints[i].enabled && state->breakpoints[i].addr == addr)
            return &state->breakpoints[i];
    }
    return NULL;
}

void bp_restore_and_step(debugger_state_t *state, breakpoint_t *bp) {
    ptrace(PTRACE_POKETEXT, state->child_pid, (void *)bp->addr,
           (void *)bp->orig_data);

    ptrace(PTRACE_SINGLESTEP, state->child_pid, NULL, NULL);
    int wait_status;
    wait(&wait_status);

    long trap_data = (bp->orig_data & ~0xFF) | 0xCC;
    ptrace(PTRACE_POKETEXT, state->child_pid, (void *)bp->addr,
           (void *)trap_data);
}
