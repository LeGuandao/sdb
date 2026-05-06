#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include "sdb.h"

int  bp_set(debugger_state_t *state, uintptr_t addr);
int  bp_del(debugger_state_t *state, int id);
void bp_list(const debugger_state_t *state);
breakpoint_t *bp_find_by_addr(debugger_state_t *state, uintptr_t addr);
void bp_restore_and_step(debugger_state_t *state, breakpoint_t *bp);

#endif
