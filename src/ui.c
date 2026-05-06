#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/history.h>
#include <readline/readline.h>

#include "ui.h"

char *ui_read_command(void) {
    char *input;
    while (1) {
        input = readline("sdb > ");
        if (input == NULL)
            return NULL;
        if (strlen(input) > 0) {
            add_history(input);
            return input;
        }
        free(input);
    }
}

const command_t *ui_find_command(const char *name, const command_t *commands) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(name, commands[i].name) == 0)
            return &commands[i];
    }
    return NULL;
}

void ui_print_help(const command_t *commands) {
    printf("Commands:\n");
    for (int i = 0; commands[i].name != NULL; i++)
        printf("  %-8s - %s\n", commands[i].name, commands[i].help);
}
