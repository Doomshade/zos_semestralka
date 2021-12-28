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
    int ret;

    memset(args.name, 0, BUFSIZ);
    register_handlers();
    if (argc == 1) { // print info
        return 0;
    }

    argp_parse(&argp, argc, argv, 0, 0, &args);

    if (strcmp(args.name, "") == 0) {
        printf("The file name cannot be empty!\n");
        return 1;
    }

    if ((ret = init_fs(args.name))) {
        printf("Could not load the file system with the given name.\n");
        return ret;
    }
    
    //test();
    //debug();

    while (1) {
        // get the cmd from stdin
        parse_cmd(stdin);

    }
    return 0;
}


#pragma clang diagnostic pop
