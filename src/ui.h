#ifndef UI_H
#define UI_H

#include "sdb.h"

char *ui_read_command(void);
const command_t *ui_find_command(const char *name, const command_t *commands);
void ui_print_help(const command_t *commands);

#endif
