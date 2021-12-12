#include "fs.h"
#include "util.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint-gcc.h>

#define SIGNATURE "jsmahy"
#define CLUSTER_SIZE ((uint16_t) 4096)
#define FIRST_FREE_INODE 1 // the first free inode, leave it at 1 as we don't need any special inodes
#define INODE_PER_CLUSTER ((double) 1 / 4) // inode per cluster ratio

// aligns the number to the "to" value
// e.g. num = 7000, to = 4096 -> this aligns to 8192
#define ALIGNED(num, to) (num) + (to - (num)) % to
#define ALIGN(num, to) (num) = ALIGNED(num, to);
#define SEEK_CLUSTER(cluster) fseek(FS->file, cluster * FS->sb->cluster_size, SEEK_SET);

// the union data_size can be either 1, 2, or 4 bytes long
#define VALIDATE_SIZE(size) if (!((size) == 1 || (size) == 2 || (size) == 4)) return 1;

#define CLUSTER_NO(addr) (addr) / FS->sb->cluster_size

#define malloc0(size) calloc(1, (size))

struct fs* FS = NULL;

union data_size {
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

static int append_cluster(uint32_t cluster, void* ptr, uint32_t size, uint32_t n) {
    SEEK_CLUSTER(cluster)

    return 0;
}

static int write_cluster(uint32_t cluster, void* ptr, uint32_t size, uint32_t n) {
    if (size * n > FS->sb->cluster_size) {
        return 1;
    }
    SEEK_CLUSTER(cluster)

    fwrite(ptr, size, n, FS->file);
    fflush(FS->file);
    return 0;
}

static int write_superblock(uint64_t disk_size) {
    // [superblock | data block bitmap | inode bitmap | inode table | data used_clusters]
    struct superblock* sb;
    uint32_t used_clusters;
    uint32_t data_bm_len;
    uint32_t inode_bm_len;
    uint32_t cluster_count;
    uint32_t i;
    uint8_t* temp_ptr;

    sb = malloc(sizeof(struct superblock));
    VALIDATE_MALLOC(sb)

    // the size of a single inode
    strcpy(sb->signature, SIGNATURE);

    sb->disk_size = disk_size;
    sb->cluster_size = CLUSTER_SIZE;
    sb->inode_size = sizeof(struct inode);

    sb->cluster_count = sb->disk_size / sb->cluster_size;
    sb->inode_count = sb->cluster_count * INODE_PER_CLUSTER; // NOLINT(cppcoreguidelines-narrowing-conversions)

    used_clusters =
            ALIGNED(ALIGNED(sb->inode_count / 8, 8), sb->cluster_size);              // the inode bitmap size in bytes

    used_clusters += ALIGNED(sb->inode_count * sb->inode_size,
                             sb->cluster_size);                          // the inode block size in bytes

    used_clusters /= sb->cluster_size;                                          // the amount of used_clusters needed for both inode bitmap and inode blocks
    used_clusters += 1;                                                         // an additional cluster for the superblock

    sb->free_cluster_count = sb->cluster_count - used_clusters;                 // the actual free cluster count
    sb->free_inode_count = sb->inode_count - 1;

    sb->data_bm_start_addr = ALIGNED(sb->cluster_size, sb->cluster_size);
    sb->inode_bm_start_addr =
            ALIGNED(sb->data_bm_start_addr + ALIGNED(sb->free_cluster_count / 8, 8), sb->cluster_size);

    sb->inode_start_addr = ALIGNED(sb->inode_bm_start_addr + ALIGNED(sb->inode_count / 8, 8), sb->cluster_size);
    sb->data_start_addr = ALIGNED(sb->inode_start_addr + sb->inode_count * sb->inode_size, sb->cluster_size);

    sb->first_ino = FIRST_FREE_INODE;

    // initialize the FS
    data_bm_len = sb->inode_bm_start_addr - sb->data_bm_start_addr;
    FS->data_bitmap = malloc0(data_bm_len * sizeof(uint8_t));

    inode_bm_len = sb->inode_start_addr - sb->inode_bm_start_addr;
    FS->inode_bitmap = malloc0(inode_bm_len * sizeof(uint8_t));

    FS->sb = sb;
    // [superblock | data block bitmap | inode bitmap | inode table | data used_clusters]
    write_cluster(0, sb, sizeof(struct superblock), 1);

    temp_ptr = FS->data_bitmap;
    cluster_count = data_bm_len / sb->cluster_size;
    for (i = 0; i < cluster_count - 1; ++i, ++temp_ptr) {
        write_cluster(i + 1, temp_ptr, sizeof(uint8_t), sb->cluster_size / sizeof(uint8_t));
    }
    write_cluster(i + 1, temp_ptr, sizeof(uint8_t), data_bm_len % sb->cluster_size);
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

int read_data(uint32_t inode_id, uint32_t* data_len, union data_size** data, uint8_t union_data_size) {
    struct inode* inode;
    int* clusters;
    union data_size* _data;
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

int write_data(uint32_t inode_id, uint32_t data_len, union data_size* data, uint8_t union_data_size) {
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
    fs->filename = filename;
    FS = fs;

    // if the file exists, attempt to load the FS from the file
    if (fexists(filename)) {
        return load_fs_from_file(filename);
    }

    return 0;
}

int fs_format(uint64_t disk_size) {
    FILE* f;

    load_file(f = fopen(FS->filename, "wb+"));
    write_superblock(disk_size);

    return 0;
}
