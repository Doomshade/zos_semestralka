#include "cmds.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "util.h"
#include "cmd_handler.h"
#include <stdlib.h>

#define VALIDATE_DIR_ENTRY(s) \
struct entry parent;\
struct entry dir;\
parse_dir((s), &parent, &dir);\
if (dir.inode_id == FREE_INODE) {\
return ERR_FILE_NOT_FOUND;\
}

#define VALIDATE_DIR(inode)\
if ((inode) == FREE_INODE || inode_get((inode))->file_type != FILE_TYPE_DIRECTORY) {\
return ERR_PATH_NOT_FOUND;\
}

int format(char* s[]) {
    uint32_t size;
    size = parse(s[0]);
    printf("%u\n", size);
    return fs_format(size) == 0 ? OK : ERR_CANNOT_CREATE_FILE;
}

int cp(char* s[]) {
    struct entry file;

    VALIDATE_DIR_ENTRY(s[1])
    // TODO probs not right
    if (!get_dir_entry_name(FS->curr_dir, &file, s[0])) {
        return ERR_FILE_NOT_FOUND;
    }
    return OK;
}

int mv(char* s[]) {
    return OK;
}

int rm(char* s[]) {
    struct entry file;

    VALIDATE_DIR_ENTRY(s[0])
    return OK;
}

int mkdir(char* a[]) {
    struct entry dir;
    struct entry parent;

    parse_dir(a[0], &parent, &dir);
    if (parent.inode_id == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }
    if (dir.inode_id != FREE_INODE) {
        return ERR_EXIST;
    }
    return create_dir(parent.inode_id, dir.item_name) != FREE_INODE ? OK : ERR_UNKNOWN;
}

int rmdir(char* a[]) {
    VALIDATE_DIR_ENTRY(a[0])
    return remove_dir(parent.inode_id, dir.item_name) ? OK : ERR_NOT_EMPTY;
}

int ls(char* a[]) {
    struct entry dir;
    struct entry parent;
    struct entry* entries;
    struct inode* inode;
    uint32_t amount;
    uint32_t i;

    parse_dir(a[0], &parent, &dir);
    VALIDATE_DIR(dir.inode_id)

    amount = get_dir_entries(dir.inode_id, &entries);
    sort_entries(&entries, amount);
    for (i = 0; i < amount; ++i) {
        inode = inode_get(entries[i].inode_id);
        printf("%s%s\n", inode->file_type == FILE_TYPE_REGULAR_FILE ? "-" : "+", entries[i].item_name);
    }
    return CUSTOM_OUTPUT;
}

int cat(char* s[]) {
    uint32_t inode;
    uint8_t* arr;

    inode = inode_from_name(FS->curr_dir, s[0]);
    if (!inode) {
        return ERR_FILE_NOT_FOUND;
    }
    arr = inode_get_contents(inode);
    printf("%s\n", arr);
    return CUSTOM_OUTPUT;
}

int cd(char* a[]) {
    struct entry dir;
    struct entry parent;

    parse_dir(a[0], &parent, &dir);

    VALIDATE_DIR(dir.inode_id)
    FS->curr_dir = dir.inode_id;
    return OK;
}

typedef struct NODE {
    struct NODE* next;
    struct entry entry;
} node;

int pwd(char* empt[]) {
    node* n;
    node* prev;
    struct entry parent;
    struct entry dir;
    struct entry entry;
    uint32_t curr_dir;

    // coding this was pain, please don't ask me how I did this
    // it was nothing but a bunch of debugging
    if (FS->curr_dir == FS->root) {
        printf("/\n");
        return CUSTOM_OUTPUT;
    }
    curr_dir = FS->curr_dir;
    prev = malloc(sizeof(node));
    VALIDATE_MALLOC(prev)

    *prev = (node) {.next = NULL, .entry = (struct entry) {.inode_id = FS->curr_dir, .item_name = CURR_DIR}};
    dir.inode_id = 0;
    while (parent.inode_id != FS->root) {
        parse_dir(PREV_DIR, &dir, &parent);
        n = malloc(sizeof(node));
        VALIDATE_MALLOC(n)

        if (!get_dir_entry_id(parent.inode_id, &entry, dir.inode_id)) {
            fprintf(stderr, "No entry %s found in dir %s! This should not happen!\n", dir.item_name, parent.item_name);
            return ERR_UNKNOWN;
        }
        n->entry = entry;
        n->next = prev;
        prev = n;
        FS->curr_dir = parent.inode_id;
    }

    while (strcmp(prev->entry.item_name, CURR_DIR) != 0) {
        printf("/%s", prev->entry.item_name);
        prev = prev->next;
    }
    printf("\n");
    FS->curr_dir = curr_dir;
    return CUSTOM_OUTPUT;
}

int info(char* as[]) {
    struct inode* inode;

    /*
    VALIDATE_DIR_ENTRY(as[0])

    // TODO bad name, cant pass dir.item_name
    if (dir.inode_id == 0) {
        get_dir_entry_name(parent.inode_id, &dir, dir.item_name);
    }*/

    inode = inode_get(inode_from_name(FS->curr_dir, as[0]));
    printf("%s - %u - %u - [%u, %u, %u, %u, %u] - %u - %u\n", as[0], inode->file_size, inode->id,
           inode->direct[0], inode->direct[1], inode->direct[2], inode->direct[3], inode->direct[4],
           inode->indirect[0], inode->indirect[1]);
    return CUSTOM_OUTPUT;
}

int incp(char* s[]) {
    FILE* f;
    uint32_t inode_id;
    uint32_t file_size;
    uint8_t* buf;
    bool success;

    f = fopen(s[0], "r");
    if (!f) {
        return ERR_FILE_NOT_FOUND;
    }
    if (!(inode_id = create_file(FS->curr_dir, s[0]))) {
        return ERR_EXIST;
    }
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    rewind(f);
    buf = malloc(file_size);
    fread(buf, file_size, 1, f);
    success = inode_write_contents(inode_id, buf, file_size);
    free(buf);
    return success ? OK : ERR_UNKNOWN;
}

int outcp(char* s[]) {
    return OK;
}

int load(char* s[]) {
    FILE* f;

    f = fopen(s[0], "r");
    if (!f) {
        return ERR_FILE_NOT_FOUND;
    }

    while (!feof(f)) {
        parse_cmd(f);
    }
    return OK;
}

int xcp(char* s[]) {
    return OK;
}

int short_cmd(char* s[]) {
    return OK;
}
