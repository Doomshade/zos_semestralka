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
#define DIRECT_TOTAL_SIZE (FS->sb->cluster_size * DIRECT_AMOUNT)
#define INDIRECT1_TOTAL_SIZE (FS->sb->cluster_size * FS->sb->cluster_size / sizeof(uint32_t))
#define INDIRECT2_TOTAL_SIZE ((FS->sb->cluster_size / sizeof(uint32_t)) * (INDIRECT1_TOTAL_SIZE))
#define MAX_FS (DIRECT_TOTAL_SIZE + INDIRECT1_TOTAL_SIZE + INDIRECT2_TOTAL_SIZE)
#define CURR_DIR "."
#define PREV_DIR ".."
#define ROOT_DIR "/"
#define DIR_SEPARATOR "/"

/**
 * The file system global variable
 */
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
};


struct inode {
    uint32_t id;            // inode ID, ID = 0 = inode_create is free
    uint8_t file_type;      // file type (REGULAR_FILE, DIRECTORY)
    uint8_t hard_links;    // the hard links
    uint32_t file_size;     // file size in bytes
    uint32_t direct[DIRECT_AMOUNT];     // 5 direct accesses to clusters
    uint32_t indirect[INDIRECT_AMOUNT];   // two indirect - one 1st rank and one 2nd rank
    const uint32_t padding[6];    // padding to reach 64 bytes
};


struct entry {
    uint32_t inode_id;
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
 * @param inode_id the inode ID
 * @return the state of an inode on disk
 */
struct inode* inode_read(uint32_t inode_id);

/**
 * Retrieves the entries in the directory
 * @param dir the dir inode
 * @param amount the entries array pointer
 * @return the amount of entries
 */
struct entry* get_dir_entries(uint32_t dir, uint32_t* amount);

/**
 * The comparison function for getting an entry
 */
typedef bool entry_compare(const struct entry entry, const void* needle);

/**
 * Removes a directory
 * @param parent the parent directory inode id
 * @param dir_name the directory name
 * @return
 */
bool remove_dir(uint32_t parent, const char dir_name[MAX_FILENAME_LENGTH]);

/**
 * Writes contents to a file
 * @param inode_id the inode id of the file
 * @param ptr the ptr to the data
 * @param size the size of the data
 * @return
 */
bool inode_write_contents(uint32_t inode_id, void* ptr, uint32_t size);

/**
 * Returns an array of file contents
 * @param inode_id the inode id
 * @param size the pointer to size
 * @return the array of file contents
 */
uint8_t* inode_get_contents(uint32_t inode_id, uint32_t* size);

/**
 * Adds an entry to the parent directory
 * @param parent the parent directory inode ID
 * @param entry the entry
 * @return
 */
bool add_entry(uint32_t parent, struct entry entry);


/**
 * Removes an entry from the parent directory
 * @param parent the parent directory inode ID
 * @param entry_name the entry inode id
 * @return
 */
bool remove_entry(uint32_t parent, const char entry_name[MAX_FILENAME_LENGTH]);


#endif //ZOS_SEMESTRALKA_FS_H
