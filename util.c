#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "fs.h"
#include "cmd_handler.h"

/**
 * Retrieves a directory entry based on the name
 * @param dir the dir inode
 * @param entry the entry pointer
 * @param property the property looked for in an entry
 * @param compare_func should return the first entry that has the given property
 * @return true if found, false otherwise
 */
static bool get_dir_entry(uint32_t dir, struct entry* entry, const void* property, entry_compare compare_func);


void remove_nl(char* s) {
    s[strcspn(s, "\n")] = 0;
}

void debug() {
    struct superblock* sb = FS->sb;
    printf("%-25s: %luB\n", "Inode size", sizeof(struct inode));
    printf("%-25s: %luB\n", "Superblock size", sizeof(struct superblock));
    printf("%-25s: %luB\n", "entry size", sizeof(struct entry));
    printf("%-25s: %luB\n", "FS (struct) size", sizeof(struct fs));
    printf("%-25s: %s\n", "filename", FS->filename);
    printf("%-25s: %u\n", "inode_count", sb->inode_count);
    printf("%-25s: %u\n", "free_inode_count", sb->free_inode_count);
    printf("%-25s: %u\n", "free_cluster_count", sb->free_cluster_count);
    printf("%-25s: %uKiB\n", "cluster_size", sb->cluster_size / 1024);
    printf("%-25s: %uB\n", "inode_size", sb->inode_size);
    printf("%-25s: %u\n", "cluster_count", sb->cluster_count);
    printf("%-25s: %u\n", "free_cluster_count", sb->free_cluster_count);
    printf("%-25s: %uMiB\n", "disk_size", (sb->disk_size / 1024) / 1024);
    printf("%-25s: %uMiB\n", "free disk size",
           (sb->disk_size - ((sb->cluster_count - sb->free_cluster_count) * sb->cluster_size)) / 1024 / 1024);
    printf("%-25s: %luGiB\n", "max filesize", MAX_FS / 1024 / 1024 / 1024);
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
        free(cmd_args[i]);
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
            fprintf(stderr, "CANNOT CREATE FILE\n");
            break;
        case ERR_FILE_NOT_FOUND:
            fprintf(stderr, "FILE NOT FOUND\n");
            break;
        case ERR_PATH_NOT_FOUND:
            fprintf(stderr, "PATH NOT FOUND\n");
            break;
        case ERR_NOT_EMPTY:
            fprintf(stderr, "NOT EMPTY\n");
            break;
        case ERR_EXIST:
            fprintf(stderr, "EXIST\n");
            break;
        case CUSTOM_OUTPUT:
            break;
        default:
            printf("An unknown error occurred\n");
            break;
    }
}

static int compare_entries(const void* aa, const void* bb) {
    struct entry* a = (struct entry*) aa;
    struct entry* b = (struct entry*) bb;
    struct inode* ia = inode_read(a->inode_id);
    struct inode* ib = inode_read(b->inode_id);
    int ret = INT32_MAX;

    if (!ia || !ib) {
        return 0;
    }

    if (strcmp(a->item_name, CURR_DIR) == 0) {
        ret = -1;
    } else if (strcmp(b->item_name, CURR_DIR) == 0) {
        ret = 1;
    } else if (strcmp(a->item_name, PREV_DIR) == 0) {
        ret = -1;
    } else if (strcmp(b->item_name, PREV_DIR) == 0) {
        ret = 1;
    } else if (ia->file_type ^ ib->file_type) {
        ret = ia->file_type == FILE_TYPE_DIRECTORY ? -1 : 1;
    } else {
        ret = strcmp(a->item_name, b->item_name);
    }
    FREE(ia);
    FREE(ib);
    return ret;
}


static bool entry_compare_name(const struct entry entry, const void* needle) {
    const char* name = (char*) needle;
    return strncmp(entry.item_name, name, MAX_FILENAME_LENGTH) == 0;
}

static bool entry_compare_id(const struct entry entry, const void* needle) {
    const uint32_t entry_id = *((uint32_t*) needle);
    return entry.inode_id == entry_id;
}

static bool get_dir_entry(uint32_t dir, struct entry* entry, const void* property, entry_compare compare_func) {
    uint32_t i = 0;
    struct inode* diri = NULL;
    struct entry* entries = NULL;
    bool found = false;
    uint32_t amount = 0;

    diri = inode_read(dir);
    if (!diri) {
        return false;
    }

    if (!(entries = get_dir_entries(dir, &amount))) {
        goto end;
    }
    for (i = 0; i < amount; ++i) {
        if (compare_func(entries[i], property)) {
            found = true;
            *entry = entries[i];
            break;
        }
    }

    FREE(entries);
    end:
    FREE(diri);
    return found;
}

bool get_dir_entry_id(uint32_t dir, struct entry* entry, uint32_t entry_id) {
    return get_dir_entry(dir, entry, &entry_id, entry_compare_id);
}

bool get_dir_entry_name(uint32_t dir, struct entry* entry, const char name[MAX_FILENAME_LENGTH]) {
    return get_dir_entry(dir, entry, name, entry_compare_name);
}

void sort_entries(struct entry** entries, uint32_t size) {
    qsort(*entries, size, sizeof(struct entry), compare_entries);
}

bool dir_has_entry(uint32_t dir, const char name[MAX_FILENAME_LENGTH]) {
    struct entry* entries = NULL;
    uint32_t count = 0;
    uint32_t i = 0;

    entries = get_dir_entries(dir, &count);
    if (!entries) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (entries[i].inode_id == FREE_INODE) {
            fprintf(stderr, "A null entry encountered, RIP\n");
            exit(1);
        }
        if (strncmp(name, entries[i].item_name, MAX_FILENAME_LENGTH) == 0) {
            FREE(entries);
            return true;
        }
    }
    FREE(entries);
    return false;
}

uint32_t inode_from_name(uint32_t dir, const char name[MAX_FILENAME_LENGTH]) {
    struct entry* entries = NULL;
    uint32_t i = 0;
    uint32_t amount = 0;
    uint32_t id = FREE_INODE;

    if (!(entries = get_dir_entries(dir, &amount))) {
        goto end;
    }

    for (i = 0; i < amount; ++i) {
        if (strcmp(entries[i].item_name, name) == 0) {
            id = entries[i].inode_id;
            break;
        }
    }
    FREE(entries);
    end:
    return id;
}

void parse_dir(const char* dir, struct entry* prev_dir, struct entry* cur_dir) {
    struct entry curr_dir;
    const struct entry EMPTY = {.inode_id = FREE_INODE, .item_name = ""};
    char* cpy;
    char* cpy_store;
    char* tok;

    cpy = malloc(sizeof(char) * (strlen(dir) + 1));
    cpy_store = cpy;
    strcpy(cpy, dir);

    // the dir starts with "/" - means it's an absolute path
    if (strncmp(cpy, ROOT_DIR, 1) == 0) {
        curr_dir = (struct entry) {.inode_id= FS->root, .item_name = ROOT_DIR};

        // it's literally only "/" -> return root
        if (strlen(cpy) == 1) {
            *cur_dir = (struct entry) curr_dir;
            *prev_dir = (struct entry) curr_dir;
            goto free;
        }
        // skip "/"
        cpy++;
    } else {
        curr_dir = (struct entry) {.inode_id = FS->curr_dir, .item_name = CURR_DIR};
    }

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
            break;
        }
    }
    free:
    free(cpy_store);
}

