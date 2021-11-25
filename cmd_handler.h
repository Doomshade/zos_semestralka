#ifndef ZOS_SEMESTRALKA_CMD_HANDLER_H
#define ZOS_SEMESTRALKA_CMD_HANDLER_H
#include "consts.h"
#define MAX_ARG_COUNT 3

void print_cmds();

void register_handlers();

int handle_cmd(char* cmd, int argc, char* argv[]);

#endif
