#include "debugger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("[*] 用法: %s <程序路径>\n", argv[0]);
        return 1;
    }

    pid_t child_pid = fork();
    if (child_pid == 0) {
        run_target(argv[1]);
    } else if (child_pid > 0) {
        debugger_state_t state;
        memset(&state, 0, sizeof(state));
        state.child_pid = child_pid;
        state.running   = 1;

        debugger_init(&state);
        debugger_run(&state);
    } else {
        perror("fork 失败");
        return 1;
    }

    return 0;
}
