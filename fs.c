#include "fs.h"
#include "util.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint-gcc.h>

#define SIGNATURE "jsmahy"
#define CLUSTER_SIZE ((uint8_t) 4096)

// the union size can be either 1, 2, or 4 bytes long
#define VALIDATE_SIZE(size) if (!((size) == 1 || (size) == 2 || (size) == 4)) return 1;

struct fs* FS = NULL;

union size {
    int8_t s_8;
    int16_t s_16;
    int32_t s_32;
};

static int load_file(FILE* f) {
    if (!f) {
        return 1;
    }
    FS->file = f;
    return 0;
}

static bool fexists(char* filename) {
    struct stat s;
    return stat(filename, &s) == 0;
}

static int load_fs_from_file(char* filename) {
    FILE* f;

    VALIDATE(load_file(f = fopen(filename, "rb+")))

    return 0;
}

static int write_superblock(uint32_t disk_size) {
    struct superblock* sb = malloc(sizeof(struct superblock));
    VALIDATE_MALLOC(sb)
    union size siz;
    siz.s_8 = 5;
    sb->disk_size = disk_size;
    sb->cluster_size = CLUSTER_SIZE;
    strcpy(sb->signature, SIGNATURE);
    return 0;
}

int get_inode(struct inode** inode, uint32_t inode_id) {
    struct inode* _inode = malloc(sizeof(struct inode));
    VALIDATE_MALLOC(_inode)

    _inode->id = inode_id;
    *inode = _inode;
    return 0;
}

int get_clusters(int** clusters, uint32_t size) {
    int* _clusters;

    if (!clusters) {
        return 1;
    }

    _clusters = calloc(10, sizeof(int));
    VALIDATE_MALLOC(_clusters)

    *clusters = _clusters;
    return 0;
}

int read_data(uint32_t inode_id, uint32_t* data_len, union size** data, uint8_t union_data_size) {
    struct inode* inode;
    int* clusters;
    union size* _data;
    int i;

    VALIDATE_MALLOC(data)
    VALIDATE_SIZE(union_data_size)
    VALIDATE(get_inode(&inode, inode_id))
    VALIDATE(get_clusters(&clusters, inode->file_size))

    *data_len = inode->file_size / union_data_size;
    _data = calloc(*data_len, union_data_size);
    VALIDATE_MALLOC(_data)

    for (i = 0; i < *data_len; ++i) {
        // TODO
        switch (union_data_size) {
            case 1:
                _data[i].s_8 = 0;
                break;
            case 2:
                _data[i].s_16 = 0;
                break;
            case 4:
                _data[i].s_32 = 0;
                break;
            default:
                return 1;
        }
    }
    *data = _data;
    return 0;
}

int write_data(uint32_t inode_id, uint32_t data_len, union size* data, uint8_t union_data_size) {
    struct inode* inode;
    int* clusters;

    VALIDATE_SIZE(union_data_size)
    VALIDATE(get_inode(&inode, inode_id))
    VALIDATE(get_clusters(&clusters, data_len * union_data_size))

    return 0;
}

int init_fs(char* filename) {
    struct fs* fs;
    FILE* f;

    fs = malloc(sizeof(struct fs));
    VALIDATE_MALLOC(fs)

    fs->fmt = false;
    FS = fs;

    // if the file exists, attempt to load the FS from the file
    if (fexists(filename)) {
        return load_fs_from_file(filename);
    }

    return 0;
}

int fs_format(uint32_t disk_size) {
    FILE* f;

    load_file(f = fopen(FS->filename, "wb+"));
    write_superblock(disk_size);

    return 0;
}
