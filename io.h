#ifndef ZOS_SEMESTRALKA_IO_H
#define ZOS_SEMESTRALKA_IO_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Finds first N (amount) "0" spots in a bitmap and writes their IDs to the array
 * @param bm_start_addr the start of the bitmap address
 * @param bm_end_addr the end of the bitmap address
 * @param amount the amount of IDS
 * @param arr the array pointer
 * @return the amount found
 */
bool find_free_spots(uint32_t bm_start_addr, uint32_t bm_end_addr, uint32_t* arr);

/**
 * Checks whether a file with some name exists
 * @param filename the file name
 * @return 0
 */
bool fexists(const char* filename);

/**
 * Checks whether a bit is set in the bitmap
 * @param bm_start_addr the start address of the bitmap
 * @param bit the bit
 * @return 0 if not, 1 if yes
 */
bool is_set_bit(uint32_t bm_start_addr, uint32_t bit);

/**
 * Sets the bit in the bitmap to 0 or 1
 * @param bm_start_addr the bitmap start address
 * @param bit the bit #
 * @param set_to_one whether to set the bit to 1
 * @return 1 if the bit was set, 0 if not
 */
bool set_bit(uint32_t bm_start_addr, uint32_t bit, bool set_to_one);

/**
 * Reads data from a cluster
 * @param cluster the cluster #
 * @param read_amount the amount to read limited to sb->cluster_size
 * @param byte_arr the array to read to
 * @param offset the offset in cluster
 * @return the amount actually read
 */
uint32_t read_cluster(uint32_t cluster, uint32_t read_amount, uint8_t* byte_arr, uint32_t offset);

/**
 * Writes data to a cluster
 * @param cluster the cluster #, if 0 is passed a new cluster will be seeked
 * @param ptr the pointer to the data
 * @param size the size of the data type
 * @param n the amount of data
 * @param offset the offset in cluster
 * @param as_data_cluster whether the cluster should be treated as a data cluster
 * (remaps cluster ID 0 to the first cluster after data_start_addr and flips a bit to 1 in the data bitmap)
 * @return the cluster ID or 0 if it could not be generated when 0 is passed
 */
uint32_t
write_cluster(uint32_t cluster_id, void* ptr, uint32_t size, uint32_t offset, bool as_data_cluster,
              bool override);


/**
 * Frees the cluster by zeroing it and flipping bit in the data bitmap to 0
 * @param cluster_id the cluster id
 * @return 0
 */
bool free_cluster(uint32_t cluster_id);

uint8_t* read_superblock();

#endif //ZOS_SEMESTRALKA_IO_H
