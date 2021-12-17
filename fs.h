#ifndef ZOS_SEMESTRALKA_FS_H
#define ZOS_SEMESTRALKA_FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define FREE_INODE 0

#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_REGULAR_FILE 1
#define FILE_TYPE_DIRECTORY 2

#define CLUSTER_SIZE 4096
#define MAX_FILENAME_LENGTH 12
#define DIRECT_AMOUNT 5
#define INDIRECT_AMOUNT 2
#define MAX_FILE_SIZE ((DIRECT_AMOUNT * CLUSTER_SIZE) + INDIRECT_AMOUNT * (CLUSTER_SIZE/4) * CLUSTER_SIZE)

extern struct fs* FS;

struct superblock {
    char signature[16];             // the author of this FS
    uint32_t inode_count;           // the total inode count
    uint32_t cluster_count;         // the total cluster count
    uint32_t free_inode_count;      // the free inode count
    uint32_t free_cluster_count;    // the free cluster count
    uint32_t disk_size;             // the total size of the disk
    uint16_t cluster_size;          // the cluster size
    uint32_t inode_bm_start_addr;   // the inode bitmap addr
    uint32_t data_bm_start_addr;    // the data bitmap addr
    uint32_t inode_start_addr;      // the addr of the first inode
    uint32_t data_start_addr;       // the addr of the first data

    uint8_t inode_size;             // the size of an inode
    uint32_t first_ino;             // first non-reserved inode_id
};


struct inode {
    uint32_t id;            // inode ID, ID = 0 = inode_create is free
    uint8_t file_type;      // file type (REGULAR_FILE, DIRECTORY)
    uint8_t hard_links;    // the hard links
    uint32_t file_size;     // file size in bytes
    uint32_t direct[DIRECT_AMOUNT];     // 5 direct accesses to clusters
    uint32_t indirect[INDIRECT_AMOUNT];   // two indirect - one 1st rank and one 2nd rank
    uint32_t padding[6];    // padding to reach 64 bytes
};


struct entry {
    const uint32_t inode_id;
    char item_name[MAX_FILENAME_LENGTH];
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
    uint32_t root;
    uint32_t curr_dir;
};

void test();


/**
 * Formats the disk with the given disk size
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

/**
 * Creates a subdirectory in the parent directory
 * @param parent_dir_inode_id the parent's inode ID
 * @param name the name of the subdirectory
 * @return the subdirectory inode id
 */
uint32_t create_dir(uint32_t parent_dir_inode_id, const char name[MAX_FILENAME_LENGTH]);

/**
 * Creates an empty file in the directory
 * @param dir_inode_id the directory inode ID
 * @param name the name of the file
 * @return the file inode id
 */
uint32_t create_file(uint32_t dir_inode_id, const char name[MAX_FILENAME_LENGTH]);

/**
 *
 * @param dir the directory
 * @param name the file/dir name
 * @return the inode ID
 */
uint32_t inode_from_name(uint32_t dir, const char name[MAX_FILENAME_LENGTH]);

/**
 *
 * @param inode_id the inode ID
 * @return the state of an inode on disk
 */
struct inode* inode_get(uint32_t inode_id);

/**
 * Checks whether the directory with the inode has an entry with some name
 * @param dir the dir inode id
 * @param name the name
 * @return true if it has, false otherwise
 */
bool dir_has_entry(uint32_t dir, const char name[MAX_FILENAME_LENGTH]);

/**
 * Retrieves the entries in the directory
 * @param dir the dir inode
 * @param _entries the entries array pointer
 * @return the amount of entries
 */
uint32_t get_dir_entries(uint32_t dir, struct entry** _entries);

int file_contents(uint32_t inode, uint8_t* arr, uint32_t* size);

#endif //ZOS_SEMESTRALKA_FS_H
