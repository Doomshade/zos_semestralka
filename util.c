#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "fs.h"
#include "cmd_handler.h"

void remove_nl(char* s) {
    s[strcspn(s, "\n")] = 0;
}

void debug() {
    struct superblock* sb = FS->sb;
    printf("%-25s: %lu\n", "Inode size", sizeof(struct inode));
    printf("%-25s: %lu\n", "Superblock size", sizeof(struct superblock));
    printf("%-25s: %lu\n", "char size", sizeof(uint8_t));
    printf("%-25s: %lu\n", "entry size", sizeof(struct entry));
    printf("%-25s: %lu\n", "FS size", sizeof(struct fs));
    printf("%-25s: %s\n", "filename", FS->filename);
    printf("%-25s: %u\n", "inode_count", sb->inode_count);
    printf("%-25s: %u\n", "free_inode_count", sb->free_inode_count);
    printf("%-25s: %u\n", "free_cluster_count", sb->free_cluster_count);
    printf("%-25s: %u\n", "cluster_size", sb->cluster_size);
    printf("%-25s: %u\n", "inode_size", sb->inode_size);
    printf("%-25s: %u\n", "cluster_count", sb->cluster_count);
    printf("%-25s: %u\n", "free_cluster_count", sb->free_cluster_count);
    printf("%-25s: %u\n", "disk_size", sb->disk_size);
    printf("%-25s: %u\n", "max filesize", MAX_FILE_SIZE);
}

uint32_t mult(char size[2]) {
    if (strncasecmp(size, "GB", 2) == 0) {
        return 1024 * mult("MB");
    }
    if (strncasecmp(size, "MB", 2) == 0) {
        return 1024 * mult("KB");
    }
    if (strncasecmp(size, "KB", 2) == 0) {
        return 1024;
    }
    return 1;
}

uint32_t parse(const char* s) {
    char* end;
    uint32_t num;

    num = strtoul(s, &end, 10);
    num *= mult(end);
    return num;
}


void parse_cmd(FILE* stream) {
    char buf[BUFSIZ];
    char cmd[BUFSIZ];
    char* cmd_args[MAX_ARG_COUNT];
    int arg_idx;
    char* token;
    int ret;
    int i;

    fgets(buf, BUFSIZ, stream);
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
            printf("OK\n");
            break;
        case ERR_CANNOT_CREATE_FILE:
            printf("CANNOT CREATE FILE\n");
            break;
        case ERR_FILE_NOT_FOUND:
            printf("FILE NOT FOUND (není zdroj)\n");
            break;
        case ERR_PATH_NOT_FOUND:
            printf("PATH NOT FOUND (neexistuje cílová cesta)\n");
            break;
        case ERR_NOT_EMPTY:
            printf("NOT EMPTY (adresář obsahuje podadresáře, nebo soubory)\n");
            break;
        case ERR_EXIST:
            printf("EXIST (nelze založit, již existuje)\n");
            break;
        case CUSTOM_OUTPUT:
            break;
        default:
            printf("An unknown error occurred\n");
            break;
    }
}

void parse_dir(const char* dir, struct entry* prev_dir, struct entry* cur_dir) {
    struct entry curr_dir;
    const struct entry EMPTY = {.inode_id = FREE_INODE, .item_name = ""};
    char* cpy;
    char* tok;

    cpy = malloc(sizeof(char) * (strlen(dir) + 1));
    strcpy(cpy, dir);

    if (strncmp(cpy, ROOT_DIR, 1) == 0) {
        curr_dir = (struct entry) {.inode_id= FS->root, .item_name = ROOT_DIR};
        if (strlen(cpy) == 1) {
            *cur_dir = (struct entry) curr_dir;
            *prev_dir = (struct entry) curr_dir;
            return;
        }
        cpy++;
    } else {
        curr_dir = (struct entry) {.inode_id = FS->curr_dir, .item_name = CURR_DIR};
    }

    // TODO must have diff results for ls and mkdir!
    tok = strtok(cpy, DIR_SEPARATOR);

    while (tok != NULL) {
        *prev_dir = (struct entry) curr_dir;
        curr_dir = (struct entry) {.inode_id = inode_from_name(curr_dir.inode_id, tok)};
        strncpy(curr_dir.item_name, tok, MAX_FILENAME_LENGTH);
        *cur_dir = (struct entry) curr_dir;
        tok = strtok(NULL, DIR_SEPARATOR);
        // we haven't gotten to the end of the directory search,
        // this means the directory does not exist
        if (tok != NULL && curr_dir.inode_id == FREE_INODE) {
            *prev_dir = (struct entry) EMPTY;
            *cur_dir = (struct entry) EMPTY;
            return;
        }
    }
}

