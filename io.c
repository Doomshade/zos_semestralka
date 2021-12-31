#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include "io.h"
#include "fs.h"
#include "util.h"

#define SIGNATURE "jsmahy"
#define FIRST_FREE_INODE 1 // the first free inode, leave it at 1 as we don't need any special inodes
#define INODE_PER_CLUSTER ((double) 1 / 4) // inode per cluster ratio

// aligns the number to the "to" value
// e.g. num = 7000, to = 4096 -> this aligns to 8192
#define SEEK_CLUSTER(cluster) fseek(FS->file, (cluster) * FS->sb->cluster_size, SEEK_SET);
#define TO_DATA_CLUSTER(cluster) (((cluster) - 1) + FS->sb->data_start_addr / FS->sb->cluster_size)
#define CAN_ADD_ENTRY_TO_DIR(dir, entry_name) ((dir)->file_type == FILE_TYPE_DIRECTORY && !dir_has_entry((dir), (entry_name)))
#define FREE_CLUSTER 0

#define BE_BIT(bit) (1 << (7 - ((bit) % 8)))

// seek at the start of the bitmap
// then seek to the byte that contains the bit
#define SEEK_BITMAP(bm_start_addr, bit) \
fseek(FS->file, (bm_start_addr), SEEK_SET); \
fseek(FS->file, (bit) / 8, SEEK_CUR); \

#define SEEK_INODE(inode_id) fseek(FS->file, FS->sb->inode_start_addr, SEEK_SET); \
fseek(FS->file, ((inode_id) - 1) * FS->sb->inode_size, SEEK_CUR);

bool is_set_bit(uint32_t bm_start_addr, uint32_t bit) {
    uint8_t byte;

    SEEK_BITMAP(bm_start_addr, bit)
    fread(&byte, sizeof(uint8_t), 1, FS->file);
    int bb = BE_BIT(bit);
    return byte & bb;
}

bool set_bit(uint32_t bm_start_addr, uint32_t bit, bool set_to_one) {
    uint8_t byte;
    uint8_t pos;
    uint8_t prev_val;

    SEEK_BITMAP(bm_start_addr, bit)
    fread(&byte, sizeof(uint8_t), 1, FS->file);

    // the position of the bit
    pos = BE_BIT(bit);
    prev_val = byte;
    if (set_to_one) {
        byte |= pos;
    } else {
        byte &= ~pos;
    }

    SEEK_BITMAP(bm_start_addr, bit)
    fwrite(&byte, sizeof(uint8_t), 1, FS->file);
    fflush(FS->file);
    return prev_val != byte ? 1 : 0;
}

bool fexists(const char* filename) {
    struct stat s;
    return stat(filename, &s) == 0;
}

uint32_t read_cluster(uint32_t cluster, uint32_t read_amount, uint8_t* byte_arr, uint32_t offset) {
    if (cluster == 0) {
        return 0;
    }
    cluster = TO_DATA_CLUSTER(cluster);
    SEEK_CLUSTER(cluster)
    fseek(FS->file, offset, SEEK_CUR);
    return fread(byte_arr, sizeof(uint8_t), read_amount, FS->file);
}

bool free_cluster(uint32_t cluster_id) {
    if (cluster_id == FREE_CLUSTER) {
        return false;
    }
    if (set_bit(FS->sb->data_bm_start_addr, TO_DATA_CLUSTER(cluster_id), false)) {
        FS->sb->free_cluster_count++;
    }
    return true;
}

uint32_t
write_cluster(uint32_t cluster_id, void* ptr, uint32_t size, uint32_t offset, bool as_data_cluster,
              bool override) {
    uint8_t arr[CLUSTER_SIZE];
    unsigned long read;
    uint32_t cluster;

    if (!ptr) {
        return 0;
    }
    cluster = cluster_id;
    if (as_data_cluster) {
        // allocate a new cluster if the cluster ID is unknown
        if (cluster_id == FREE_CLUSTER) {
            if (!find_free_spots(FS->sb->data_bm_start_addr, FS->sb->inode_bm_start_addr, &cluster_id)) {
                fprintf(stderr, "Ran out of clusters!\n");
                exit(1);
            }
            offset = 0;
            // increment the ID by one because we will get the position of the cluster ID,
            // which is indexed from 0, but we index from 1
            cluster_id++;
        }
        cluster = TO_DATA_CLUSTER(cluster_id);
    }

    if (!override) {
        // we decremented the cluster ID because we index from 0, need to add 1 to the read
        // cluster function
        read = read_cluster(cluster_id, offset, arr, 0);
        if (read + size > CLUSTER_SIZE) {
            fprintf(stderr, "Attempted to override data after cluster!\n");
            exit(1);
        }
        memcpy(arr + read, ptr, size);
        size += read;
    } else {
        memcpy(arr, ptr, size);
    }
    // write the data
    SEEK_CLUSTER(cluster)
    fwrite(arr, size, 1, FS->file);
    fflush(FS->file);

    // mark the cluster ID as used
    if (as_data_cluster) {
        if (set_bit(FS->sb->data_bm_start_addr, cluster_id - 1, true)) {
            FS->sb->free_cluster_count--;
        }
    }

    return cluster_id;
}

bool find_free_spots(uint32_t bm_start_addr, uint32_t bm_end_addr, uint32_t* arr) {
    uint8_t* data;
    uint32_t i;
    uint32_t len;
    uint8_t j;

    len = bm_end_addr - bm_start_addr;
    data = malloc(len * sizeof(uint8_t));
    VALIDATE_MALLOC(data)
    SEEK_BITMAP(bm_start_addr, 0)
    fread(data, sizeof(uint8_t), len, FS->file);

    for (i = 0; i < len; ++i) {
        for (j = 0; j < 8; ++j, data[i] <<= 1) {
            if (!(data[i])) {
                *arr = i * 8 + j;
                free(data);
                return true;
            }
        }
    }
    free(data);
    return false;
}
