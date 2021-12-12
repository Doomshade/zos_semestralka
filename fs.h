#ifndef ZOS_SEMESTRALKA_FS_H
#define ZOS_SEMESTRALKA_FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define FREE_INODE 0
#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_REGULAR_FILE 1
#define FILE_TYPE_DIRECTORY 2

extern struct fs* FS;

struct superblock {
    char signature[16];             // the author of this FS
    uint32_t inode_count;           // the total inode count
    uint32_t cluster_count;         // the total cluster count
    uint32_t free_inode_count;      // the free inode count
    uint32_t free_cluster_count;    // the free cluster count
    uint64_t disk_size;             // the total size of the disk
    uint16_t cluster_size;          // the cluster size
    uint32_t inode_bm_start_addr;   // the inode bitmap addr
    uint32_t data_bm_start_addr;    // the data bitmap addr
    uint32_t inode_start_addr;      // the addr of the first inode
    uint32_t data_start_addr;       // the addr of the first data

    uint8_t inode_size;             // the size of an inode
    uint32_t first_ino;             // first non-reserved inode_id
};


struct inode {
    uint32_t id;            // inode ID, ID = 0 = get_inode is free
    uint8_t file_type;      // file type (REGULAR_FILE, DIRECTORY)
    uint8_t references;     // reference count for hardlinks
    uint32_t file_size;     // file size in bytes
    uint32_t direct[5];     // 5 hard links
    uint32_t indirect[2];   // two indirect links
    uint32_t padding[6];    // padding to reach 64 bytes
};


struct entry {
    uint32_t inode_id;
    char item_name[12];
};

/**
 * The filesystem. The layout is as follows:
 * [superblock | data block bitmap | inode bitmap | inode table | data blocks]
 */
struct fs {
    struct superblock* sb;
    bool fmt;
    FILE* file;
    char* filename;
    struct entry* curr_dir;

    uint8_t* inode_bitmap;
    uint8_t* data_bitmap;
};

/**
 * Formats the disk with the given disk size
 * @param disk_size the disk size
 * @return
 */
int fs_format(uint64_t disk_size);

/**
 * Loads or initializes the file system from the file with the given name
 * @param filename the file name
 * @return
 */
int init_fs(char* filename);

int fs_mkdir(uint32_t parent_id, char name[12]);

#endif //ZOS_SEMESTRALKA_FS_H
