#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "sdb.h"

void run_target(const char *program);
void debugger_init(debugger_state_t *state);
void debugger_run(debugger_state_t *state);

#endif
