#include <stdlib.h>
#include <argp.h>
#include <string.h>
#include "cmd_handler.h"
#include "fs.h"
#include "util.h"

const char* argp_program_version = "ZOS Semestralka ";
const char* argp_program_bug_address = "<jsmahy@students.zcu.cz>";

static char doc[] = "Semestralni prace ZOS 2021.";

// a description of the arguments we accept.
static char args_doc[] = "ARG1";

// the options we understand.
static struct argp_option options[] = {
        {"file", 'f', "", 0, "The file name"},
        {0}
};

// used by main to communicate with parse_opt.
struct arguments {
    char name[BUFSIZ];
};

// parse a single option.
static error_t parse_opt(int key, char* arg, struct argp_state* state) {
    // get the input argument from argp_parse, which we
    // know is a pointer to our arguments structure.
    struct arguments* arguments = state->input;

    switch (key) {
        case 'f':
            strncpy(arguments->name, arg, BUFSIZ);
        case ARGP_KEY_ARG:
        case ARGP_KEY_END:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

// our argp parser
static struct argp argp = {options, parse_opt, args_doc, doc};

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"

int main(int argc, char* argv[]) {
    struct arguments args;
    char buf[BUFSIZ];
    char cmd[BUFSIZ];
    char* cmd_args[MAX_ARG_COUNT];
    int arg_idx;
    char* token;
    int ret;
    int i;

    memset(args.name, 0, BUFSIZ);
    register_handlers();
    if (argc == 1) { // print info
        return 0;
    }


    printf("Inode size: %lu\n", sizeof(struct inode));
    printf("Superblock size: %lu\n", sizeof(struct superblock));
    printf("char size: %lu\n", sizeof(uint8_t));
    printf("entry size: %lu\n", sizeof(struct entry));
    argp_parse(&argp, argc, argv, 0, 0, &args);

    if (strcmp(args.name, "") == 0) {
        printf("The file name cannot be empty!\n");
        return 1;
    }

    if ((ret = init_fs(args.name))) {
        printf("Could not load the file system with the given name.\n");
        return ret;
    }

    while (1) {
        // get the cmd from stdin
        fgets(buf, BUFSIZ, stdin);
        token = strtok(buf, " ");
        strncpy(cmd, token, BUFSIZ);

        // get the arguments from stdin
        arg_idx = 0;
        while ((token = strtok(NULL, " ")) != NULL && arg_idx < MAX_ARG_COUNT) {
            cmd_args[arg_idx] = malloc(sizeof(char) * (strlen(token) + 1));
            strcpy(cmd_args[arg_idx], token);
            arg_idx++;
        }

        // get rid of "\n" from both the cmd and the last argument
        remove_nl(cmd);
        if (arg_idx - 1 >= 0) {
            remove_nl(cmd_args[arg_idx - 1]);
        }

        // handle the command with given args
        ret = handle_cmd(cmd, arg_idx, cmd_args);
        for (i = 0; i < arg_idx; ++i) {
            free(cmd_args[arg_idx]);
        }

        switch (ret) {
            case CMD_NOT_FOUND:
                printf("Invalid command!\n");
                print_cmds();
                break;
            case FS_NOT_YET_FORMATTED:
                printf("You must format the disk first!\n");
                print_cmd("format");
                break;
            case INVALID_ARG_AMOUNT:
                printf("Invalid amount of arguments!\n");
                print_cmd(cmd);
                break;
            case OK:
                break;
            default:
                printf("An unknown error occurred\n");
                break;
        }
    }
    return 0;
}

#pragma clang diagnostic pop
