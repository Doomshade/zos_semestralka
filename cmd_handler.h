#ifndef ZOS_SEMESTRALKA_CMD_HANDLER_H
#define ZOS_SEMESTRALKA_CMD_HANDLER_H
#define MAX_ARG_COUNT 3

#define ERR_UNKNOWN (-0x81)
#define CUSTOM_OUTPUT (-0x80)
#define OK 0
#define ERR_FILE_NOT_FOUND 1
#define ERR_PATH_NOT_FOUND 2
#define ERR_EXIST 3
#define ERR_NOT_EMPTY 4
#define ERR_CANNOT_CREATE_FILE 5
#define CMD_NOT_FOUND 0x80
#define INVALID_ARG_AMOUNT 0x81
#define FS_NOT_YET_FORMATTED 0x82

/**
 * Prints all commands
 */
void print_cmds();

/**
 * Prints a specific command
 * @param cmd the command name
 */
void print_cmd(char* cmd);

/**
 * Registers the command handlers
 */
void register_handlers();

/**
 * Handles a command with the arguments
 * @param cmd the command
 * @param argc the amount of arguments
 * @param argv the arguments
 * @return
 */
int handle_cmd(char* cmd, int argc, char* argv[]);

#endif
