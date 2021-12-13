#include "util.h"
#include <string.h>
#include "fs.h"

void remove_nl(char* s) {
    s[strcspn(s, "\n")] = 0;
}

void debug() {
    struct superblock* sb = FS->sb;
    printf("%-25s: %lu\n", "Inode size", sizeof(struct inode));
    printf("%-25s: %lu\n", "Superblock size", sizeof(struct superblock));
    printf("%-25s: %lu\n", "char size", sizeof(uint8_t));
    printf("%-25s: %lu\n", "entry size", sizeof(struct entry));
    printf("%-25s: %lu\n", "FS size", sizeof(struct fs));
    printf("%-25s: %s\n", "filename", FS->filename);
    printf("%-25s: %u\n", "inode_count", sb->inode_count);
    printf("%-25s: %u\n", "free_inode_count", sb->free_inode_count);
    printf("%-25s: %u\n", "free_cluster_count", sb->free_cluster_count);
    printf("%-25s: %u\n", "cluster_size", sb->cluster_size);
    printf("%-25s: %u\n", "inode_size", sb->inode_size);
    printf("%-25s: %u\n", "cluster_count", sb->cluster_count);
    printf("%-25s: %u\n", "free_cluster_count", sb->free_cluster_count);
    printf("%-25s: %u\n", "disk_size", sb->disk_size);
}