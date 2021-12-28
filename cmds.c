#include "cmds.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "util.h"
#include "cmd_handler.h"
#include <stdlib.h>

#define PARSE_PATHS(arr, amount) \
uint32_t i;\
struct entry child[amount];\
struct entry parent[amount];     \
memset(parent, 0, sizeof(struct entry) * amount);                                 \
memset(child, 0, sizeof(struct entry) * amount);                                 \
for(i = 0; i < (amount); ++i){     \
parse_dir((arr)[i], &parent[i], &child[i]);\
}

#define VALIDATE_DIR(_inode) \
struct inode* nnode = NULL;                            \
if ((_inode) == FREE_INODE || (nnode = inode_get((_inode)))->file_type != FILE_TYPE_DIRECTORY) { \
FREE(nnode);                            \
return ERR_PATH_NOT_FOUND;\
}                           \
FREE(nnode);

int format(char* s[]) {
    uint32_t size;
    size = parse(s[0]);
    return fs_format(size) == 0 ? OK : ERR_CANNOT_CREATE_FILE;
}

int cp(char* s[]) {
    struct inode* from_inode;
    uint32_t to_inode;
    bool success = false;
    uint8_t* arr;
    uint32_t size;

    // parse the "from" and "to" paths
    PARSE_PATHS(s, 2)

    // check if the file "from" exists
    if (child[0].inode_id == FREE_INODE) {
        return ERR_FILE_NOT_FOUND;
    }

    from_inode = inode_get(child[0].inode_id);
    if (!from_inode) {
        return ERR_UNKNOWN;
    }

    // check if the dir "to" exists
    if (parent[1].inode_id == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }

    // check if the to_entry is a dir or a name
    // copy the contents
    arr = inode_get_contents(from_inode->id, &size);

    // check if the child was non-existent
    if (child[1].inode_id == FREE_INODE) {
        to_inode = create_file(parent[1].inode_id, child[1].item_name);
    } else {
        to_inode = create_file(child[1].inode_id, child[0].item_name);
        if (to_inode == FREE_INODE){
            return ERR_EXIST;
        }
    }
    success = inode_write_contents(to_inode, arr, size);
    // and then write them
    FREE(arr)
    return success ? OK : ERR_UNKNOWN;
}

int mv(char* s[]) {
    struct entry new_entry;
    struct inode* inode;

    // parse the "from" and "to" dir/file
    PARSE_PATHS(s, 2)
    // entry id 0  <==> the file is to be renamed or renamed and moved
    // entry id >0 <==> inode is a directory? -> the file is to be moved
    //                  inode is a file?      -> a file with that name already exists, don't move
    if (child[1].inode_id == FREE_INODE) {
        goto cmd;
    }

    inode = inode_get(child[1].inode_id);
    if (!inode) {
        return ERR_UNKNOWN;
    }
    if (inode->file_type != FILE_TYPE_DIRECTORY) {
        if (inode->file_type == FILE_TYPE_REGULAR_FILE) {
            return ERR_EXIST;
        }
        return ERR_UNKNOWN;
    }

    cmd:
    remove_entry(parent[0].inode_id, child[0].item_name);
    new_entry = (struct entry) {.inode_id = child[0].inode_id};
    // we are renaming the file
    if (child[1].inode_id == FREE_INODE) {
        strncpy(new_entry.item_name, child[1].item_name, MAX_FILENAME_LENGTH);
        add_entry(parent[1].inode_id, new_entry);
    }
        // we are moving the file
    else {
        strncpy(new_entry.item_name, child[0].item_name, MAX_FILENAME_LENGTH);
        add_entry(child[1].inode_id, new_entry);
    }

    return OK;
}

int rm(char* s[]) {
    struct entry file;
    struct entry dir;

    parse_dir(s[0], &dir, &file);
    return remove_entry(dir.inode_id, file.item_name) ? OK : ERR_FILE_NOT_FOUND;
}

int mkdir(char* a[]) {
    struct entry dir;
    struct entry parent;

    parse_dir(a[0], &parent, &dir);

    // if the parent dir doesn't exist, the path doesn't exist
    if (parent.inode_id == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }

    // the dir already exists
    if (dir.inode_id != FREE_INODE) {
        return ERR_EXIST;
    }
    return create_dir(parent.inode_id, dir.item_name) != FREE_INODE ? OK : ERR_UNKNOWN;
}

int rmdir(char* a[]) {
    struct entry parent;
    struct entry dir;

    parse_dir(a[0], &parent, &dir);
    if (dir.inode_id == FREE_INODE) {
        return ERR_FILE_NOT_FOUND;
    }
    return remove_dir(parent.inode_id, dir.item_name) ? OK : ERR_NOT_EMPTY;
}

int ls(char* a[]) {
    struct entry dir = EMPTY_ENTRY;
    struct entry parent = EMPTY_ENTRY;
    struct entry* entries = NULL;
    struct inode* inode = NULL;
    uint32_t amount = 0;
    uint32_t i = 0;

    // parse the input and check whether it's a directory
    parse_dir(a[0], &parent, &dir);
    VALIDATE_DIR(dir.inode_id)

    // get the directory entries
    entries = get_dir_entries(dir.inode_id, &amount);
    if (!entries) {
        return ERR_UNKNOWN;
    }
    // sort them
    sort_entries(&entries, amount);

    // print each entry
    for (i = 0; i < amount; ++i) {
        inode = inode_get(entries[i].inode_id);
        printf("%s%s\n", inode->file_type == FILE_TYPE_REGULAR_FILE ? "-" : "+", entries[i].item_name);
        FREE(inode);
    }
    FREE(entries);
    return CUSTOM_OUTPUT;
}

int cat(char* s[]) {
    struct entry file;
    struct entry dir;
    uint32_t inode_id;
    struct inode* inode;
    uint8_t* arr;
    uint32_t size;

    parse_dir(s[0], &dir, &file);

    // get the inode_id
    inode_id = inode_from_name(dir.inode_id, file.item_name);
    if (inode_id == FREE_INODE) {
        return ERR_FILE_NOT_FOUND;
    }

    inode = inode_get(inode_id);
    if (!inode) {
        return ERR_UNKNOWN;
    }

    // the file must be a regular file
    if (inode->file_type != FILE_TYPE_REGULAR_FILE) {
        return ERR_FILE_NOT_FOUND;
    }
    // print the contents
    arr = inode_get_contents(inode_id, &size);
    printf("%s\n", arr);
    FREE(arr);
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
    node* copy;
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
    copy = prev;

    *prev = (node) {.next = NULL, .entry = (struct entry) {.inode_id = FS->curr_dir, .item_name = CURR_DIR}};
    dir.inode_id = 0;
    parent = (struct entry) {.inode_id=0, .item_name=""};

    // TODO free mallocs
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
    while (copy) {
        prev = copy;
        copy = prev->next;
        FREE(prev);
    }
    printf("\n");
    FS->curr_dir = curr_dir;
    return CUSTOM_OUTPUT;
}

int info(char* as[]) {
    struct entry file;
    struct entry dir;
    struct inode* inode;

    parse_dir(as[0], &dir, &file);

    inode = inode_get(inode_from_name(dir.inode_id, file.item_name));
    if (!inode) {
        return ERR_FILE_NOT_FOUND;
    }
    printf("%s%s - %u - %u - [%u, %u, %u, %u, %u] - %u - %u\n", file.item_name,
           (inode->file_type == FILE_TYPE_DIRECTORY ? "/" : ""), inode->file_size, inode->id,
           inode->direct[0], inode->direct[1], inode->direct[2], inode->direct[3], inode->direct[4],
           inode->indirect[0], inode->indirect[1]);
    return CUSTOM_OUTPUT;
}

int incp(char* s[]) {
    struct entry dir_from;
    struct entry file_from;
    struct entry dir_to;
    struct entry file_to;

    FILE* f = NULL;
    uint32_t inode_id = 0;
    uint32_t file_size = 0;
    struct inode* inode;
    uint8_t* buf = NULL;
    bool success = false;

    f = fopen(s[0], "r");
    if (!f) {
        return ERR_FILE_NOT_FOUND;
    }

    parse_dir(s[0], &dir_from, &file_from);
    parse_dir(s[1], &dir_to, &file_to);

    // now we need to check whether "file_to" is a directory or nonexistent
    // it doesn't exist, means we create a new file with that name
    if (file_to.inode_id == FREE_INODE) {
        if ((inode_id = create_file(dir_to.inode_id, file_to.item_name)) == FREE_INODE) {
            return ERR_EXIST;
        }
    }
        // the file does exist, check whether it's a directory or an already existing file
    else {
        inode = inode_get(file_to.inode_id);
        if (!inode) {
            return ERR_UNKNOWN;
        }

        // if the inode is a file      -> a file with that name already exists, throw a warning
        // if the inode is a directory -> we create a new file with the same name in our FS
        switch (inode->file_type) {
            case FILE_TYPE_REGULAR_FILE:
                return ERR_EXIST;
            case FILE_TYPE_DIRECTORY:
                // a file with that name could already exist
                if ((inode_id = create_file(file_to.inode_id, file_from.item_name)) == FREE_INODE) {
                    return ERR_EXIST;
                }
                break;
            default:
                return ERR_UNKNOWN;
        }
    }

    // malloc a buffer for the file, stack would probs die otherwise lol
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    rewind(f);
    buf = malloc(file_size);

    // read the file_to and write the contents
    fread(buf, file_size, 1, f);
    success = inode_write_contents(inode_id, buf, file_size);
    FREE(buf);
    return success ? OK : ERR_UNKNOWN;
}

int outcp(char* s[]) {
    struct entry dir_from;
    struct entry file_from;
    struct entry dir_to;
    struct entry file_to;

    FILE* f;
    uint32_t inode_id;
    uint8_t* arr;
    uint32_t size;

    parse_dir(s[0], &dir_from, &file_from);
    parse_dir(s[1], &dir_to, &file_to);

    if (file_from.inode_id == FREE_INODE) {
        return ERR_FILE_NOT_FOUND;
    }

    inode_id = inode_from_name(dir_from.inode_id, file_from.item_name);
    if (!inode_id) {
        return ERR_FILE_NOT_FOUND;
    }

    arr = inode_get_contents(inode_id, &size);
    f = fopen(s[1], "w");
    if (!f) {
        return ERR_PATH_NOT_FOUND;
    }
    rewind(f);
    fwrite(arr, size, 1, f);
    fflush(f);

    FREE(arr);
    return OK;
}

int load(char* s[]) {
    FILE* f;

    f = fopen(s[0], "r");
    if (!f) {
        return ERR_FILE_NOT_FOUND;
    }

    printf("++++++++++\t%s\t++++++++++\n", s[0]);
    while (!feof(f)) {
        parse_cmd(f);
    }
    printf("----------\t%s\t----------\n", s[0]);
    return OK;
}

#define XCP_ARGC 3

int xcp(char* s[]) {
    struct entry dir[XCP_ARGC];
    struct entry file[XCP_ARGC];
    uint8_t* contents[XCP_ARGC];
    uint32_t size[XCP_ARGC];
    uint32_t i;
    uint32_t inode;
    bool success = false;

    // parse the dirs/files from the input
    for (i = 0; i < XCP_ARGC; ++i) {
        parse_dir(s[i], &dir[i], &file[i]);
        if (i != (XCP_ARGC - 1) && file[i].inode_id == FREE_INODE) {
            return ERR_FILE_NOT_FOUND;
        }
    }

    // copy their contents and add the size
    for (i = 0; i < XCP_ARGC - 1; ++i) {
        contents[i] = inode_get_contents(file[i].inode_id, &size[i]);
        size[XCP_ARGC - 1] += size[i];
    }

    // copy the contents to the file
    contents[XCP_ARGC - 1] = malloc(size[XCP_ARGC - 1]);
    for (i = 0; i < XCP_ARGC - 1; ++i) {
        memcpy(contents[XCP_ARGC - 1] + (i > 0 ? size[i - 1] : 0), contents[i], size[i]);
    }

    // create a new file and write the contents
    inode = create_file(dir[XCP_ARGC - 1].inode_id, file[XCP_ARGC - 1].item_name);
    success = inode_write_contents(inode, contents[XCP_ARGC - 1], size[XCP_ARGC - 1]);
    FREE(contents[XCP_ARGC - 1])
    return success ? OK : ERR_UNKNOWN;
}

#define SHORT_SIZE 5000

int short_cmd(char* s[]) {
    struct entry file;
    struct entry dir;
    struct inode* inode;
    bool success = true;
    uint8_t* arr;
    uint32_t size;

    // parse the file and get the inode
    parse_dir(s[0], &dir, &file);
    inode = inode_get(file.inode_id);
    if (!inode) {
        return ERR_FILE_NOT_FOUND;
    }
    if (inode->file_size <= SHORT_SIZE) {
        goto end;
    }
    arr = inode_get_contents(inode->id, &size);
    success = inode_write_contents(inode->id, arr, SHORT_SIZE);

    FREE(arr);
    end:
    FREE(inode);
    return success ? OK : ERR_UNKNOWN;
}
