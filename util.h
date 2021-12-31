#ifndef ZOS_SEMESTRALKA_UTIL_H
#define ZOS_SEMESTRALKA_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "fs.h"

#define EMPTY_ENTRY (struct entry) {.inode_id = FREE_INODE, .item_name = ""}
#define MAX(a, b) (a) > (b) ? (a) : (b)
#define MIN(a, b) (a) < (b) ? (a) : (b)
#define VALIDATE(ret) if (ret) return ret;
#define FREE(m) if((m)) free((m));
#define VALIDATE_MALLOC(data) if (!(data)){ \
fprintf(stderr, "Ran out of memory!\n");                                          \
exit(-1);                                          \
}

/**
 * Removes the new line from the string s
 * @param s the string
 */
void remove_nl(char* s);

/**
 * Lists out all FS and SB params
 */
void debug();

/**
 * Parses the number or exits if the number could not be parsed
 * @param s the number
 * @return
 */
uint32_t parse(const char* s);

/**
 * Parses the dir
 * @param dir the full dir name
 * @param out_dir the dir name ptr
 * @param prev_dir the ptr to prev_dir
 * @param cur_dir the ptr to cur_dir
 * @return the dir inode ID
 */
void parse_dir(const char* dir, struct entry* prev_dir, struct entry* cur_dir);

void parse_cmd(FILE* stream);


/**
 * Sorts the entries
 * @param entries the entries
 * @param size the amount of entries
 */
void sort_entries(struct entry** entries, uint32_t size);

/**
 * Retrieves a directory entry based on the name
 * @param dir the dir inode
 * @param entry the entry pointer
 * @param entry_id the entry inode id
 * @return true if found, false otherwise
 */
bool get_dir_entry_id(uint32_t dir, struct entry* entry, uint32_t entry_id);

/**
 * Retrieves a directory entry based on the name
 * @param dir the dir inode
 * @param entry the entry pointer
 * @param name the entry name
 * @return true if found, false otherwise
 */
bool get_dir_entry_name(uint32_t dir, struct entry* entry, const char name[MAX_FILENAME_LENGTH]);


/**
 * Checks whether the directory with the inode has an entry with some name
 * @param dir the dir inode id
 * @param name the name
 * @return true if it has, false otherwise
 */
bool dir_has_entry(uint32_t dir, const char name[MAX_FILENAME_LENGTH]);

/**
 * Retrieves the ID of the inode by the name
 * @param dir the directory
 * @param name the file/dir name
 * @return the inode ID
 */
uint32_t inode_from_name(uint32_t dir, const char name[MAX_FILENAME_LENGTH]);

#endif //ZOS_SEMESTRALKA_UTIL_H
