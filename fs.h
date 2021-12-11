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
    char signature[9];              // login autora FS
    char vol_desc[251];             // volume desc

    uint32_t inode_count;           //
    uint32_t cluster_count;          //pocet clusteru
    uint32_t free_inode_count;
    uint32_t free_cluster_count;
    uint32_t disk_size;              //celkova velikost VFS
    uint8_t cluster_size;           //velikost clusteru
    uint32_t bitmapi_start_address;  //adresa pocatku bitmapy i-uzlů
    uint32_t bitmap_start_address;   //adresa pocatku bitmapy datových bloků
    uint32_t inode_start_address;    //adresa pocatku  i-uzlů
    uint32_t data_start_address;     //adresa pocatku datovych bloku
    uint32_t first_ino;              // first non-reserved inode_id
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

struct fs {
    struct superblock* sb;
    bool fmt;
    FILE* file;
    char* filename;
    struct entry* curr_dir;
};

/**
 * Initializes the superblock with the given disk size
 * @param disk_size the disk size
 * @return
 */
int fs_format(uint32_t disk_size);

/**
 * Loads or initializes the file system from the file with the given name
 * @param filename the file name
 * @return
 */
int init_fs(char* filename);

int fs_mkdir(uint32_t parent_id, char name[12]);

#endif //ZOS_SEMESTRALKA_FS_H
