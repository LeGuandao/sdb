#ifndef SDB_H
#define SDB_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/user.h>

#define MAX_BREAKPOINTS 64

typedef struct {
    int      id;
    uintptr_t addr;
    long     orig_data;
    int      enabled;
} breakpoint_t;

typedef struct debugger_state debugger_state_t;

typedef void (*cmd_handler_t)(debugger_state_t *);

typedef struct {
    const char    *name;
    const char    *help;
    cmd_handler_t  handler;
} command_t;

struct debugger_state {
    pid_t         child_pid;
    breakpoint_t  breakpoints[MAX_BREAKPOINTS];
    int           bp_count;
    int           running;
    int           step_count;
};

#endif
