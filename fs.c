#include "fs.h"
#include "util.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint-gcc.h>
#include <unistd.h>

#define SIGNATURE "jsmahy"
#define CLUSTER_SIZE ((uint16_t) 4096)
#define FIRST_FREE_INODE 1 // the first free inode, leave it at 1 as we don't need any special inodes
#define INODE_PER_CLUSTER ((double) 1 / 4) // inode per cluster ratio

// aligns the number to the "to" value
// e.g. num = 7000, to = 4096 -> this aligns to 8192
#define ALIGNED_UP(num, to) (num) + ((to) - (num)) % (to)
#define ALIGNED_DOWN(num, to) (num) % (to) == 0 ? (num) : ALIGNED_UP((num), (to)) - (to)
#define ALIGN_UP(num, to) (num) = ALIGNED_UP((num), (to));
#define ALIGN_DOWN(num, to) (num) = ALIGNED_DOWN((num), (to));
#define SEEK_CLUSTER(cluster) fseek(FS->file, (cluster) * FS->sb->cluster_size, SEEK_SET);
#define TO_DATA_CLUSTER(cluster) (cluster) + FS->sb->data_start_addr / FS->sb->cluster_size
#define SEEK_DATA_CLUSTER(cluster) SEEK_CLUSTER(TO_DATA_CLUSTER((cluster)))

#define LE_BIT(bit) (1 << (bit) % 8)
#define BE_BIT(bit) (1 << (7 - ((bit) % 8)))

// seek at the start of the bitmap
// then seek to the byte that contains the bit
#define SEEK_BITMAP(bm_start_addr, bit) \
fseek(FS->file, (bm_start_addr), SEEK_SET); \
fseek(FS->file, (bit) / 8, SEEK_CUR); \

#define SEEK_INODE(inode_id) fseek(FS->file, FS->sb->inode_start_addr, SEEK_SET); \
fseek(FS->file, inode_id * FS->sb->inode_size, SEEK_CUR);

#define READ_BYTES(start_addr, end_addr, arr) fseek(FS->file, (start_addr), SEEK_SET); \
fread((arr), sizeof(uint8_t), (end_arr) - (start_addr), FS->file);

// the union data_size can be either 1, 2, or 4 bytes long
#define VALIDATE_SIZE(size) if (!((size) == 1 || (size) == 2 || (size) == 4)) return 1;

#define malloc0(size) calloc(1, (size))

/**
 * Writes an inode to the disk
 * @param inode the inode
 * @return 0 if the inode was written correctly
 */
static int write_inode_to_disk(struct inode* inode);

/**
 * Reads an inode from the disk
 * @param inode_id the inode id
 * @return the inode or NULL if an inode with that id doesn't exist or is invalid
 */
static struct inode* read_inode_from_disk(uint32_t inode_id);

/**
 * Creates an inode
 * @return 0 if the inode could not be created, otherwise the inode ID
 */
static uint32_t create_inode();

/**
 * Finds first N (amount) "0" spots in a bitmap and writes their IDs to the array
 * @param bm_start_addr the start of the bitmap address
 * @param bm_end_addr the end of the bitmap address
 * @param amount the amount of IDS
 * @param arr the array pointer
 * @return the amount found
 */
static uint32_t find_free_spots(uint32_t bm_start_addr, uint32_t bm_end_addr, uint32_t amount, uint32_t* arr);

/**
 * Frees the cluster by zeroing it and flipping bit in the data bitmap to 0
 * @param cluster_id the cluster id
 * @return 0
 */
static int free_cluster(uint32_t cluster_id);

/**
 * Checks whether a file with some name exists
 * @param filename the file name
 * @return 0
 */
static bool fexists(char* filename);

/**
 * Checks whether a bit is set in the bitmap
 * @param bm_start_addr the start address of the bitmap
 * @param bit the bit
 * @return 0 if not, 1 if yes
 */
static int is_set_bit(uint32_t bm_start_addr, uint32_t bit);

/**
 * Sets the bit in the bitmap to 0 or 1
 * @param bm_start_addr the bitmap start address
 * @param bit the bit #
 * @param set_to_one whether to set the bit to 1
 * @return 1 if the bit was set, 0 if not
 */
static int set_bit(uint32_t bm_start_addr, uint32_t bit, bool set_to_one);

/**
 * Sets the FS->file if the file exists
 * @param f the file
 * @return 0 if the file was set, 1 otherwise
 */
static int load_file(FILE* f);

/**
 * Loads the file system from a file with the name
 * @param filename the file name
 * @return
 */
static int load_fs_from_file(char* filename);

/**
 * Reads data from a cluster
 * @param cluster the cluster #
 * @param read_amount the amount to read limited to sb->cluster_size
 * @param byte_arr the array to read to
 * @param offset the offset in cluster
 * @return the amount actually read
 */
static uint32_t read_cluster(uint32_t cluster, uint32_t read_amount, uint8_t* byte_arr, uint32_t offset);

/**
 * Writes data to a cluster
 * @param cluster the cluster #
 * @param ptr the pointer to the data
 * @param size the size of the data type
 * @param n the amount of data
 * @param offset the offset in cluster
 * @param as_data_cluster whether the cluster should be treated as a data cluster
 * (remaps cluster ID 0 to the first cluster after data_start_addr and flips a bit to 1 in the data bitmap)
 * @return 0 if written successfully, 1 otherwise
 */
static int
write_cluster(uint32_t cluster_id, void* ptr, uint32_t size, uint32_t n, uint32_t offset, bool as_data_cluster,
              bool override);

/**
 * Initializes the superblock with the given disk size and
 * @param disk_size the disk size
 * @param _sb the superblock pointer
 * @return 0 if successfully initialized
 */
static int init_superblock(uint32_t disk_size, struct superblock** _sb);

/**
 * The variably data size (1/2/4 byte)
 */
union data_size {
    int8_t s_8;
    int16_t s_16;
    int32_t s_32;
};

/**
 * The file system global variable definition.
 * [superblock | data bitmap | inode bitmap | inodes | data]
 */
struct fs* FS = NULL;

static uint32_t find_free_spots(uint32_t bm_start_addr, uint32_t bm_end_addr, uint32_t amount, uint32_t* arr) {
    uint32_t found;
    uint8_t* data;
    uint32_t i;
    uint32_t len;
    uint8_t j;

    len = bm_end_addr - bm_start_addr;
    VALIDATE_MALLOC(data = malloc(len * sizeof(uint8_t)))
    SEEK_BITMAP(bm_start_addr, 0)
    fread(data, sizeof(uint8_t), len, FS->file);

    found = 0;
    for (i = 0; i < len && found < amount; ++i) {
        for (j = 0; j < 8 && found < amount; ++j, data[i] <<= 1) {
            if (!(data[i])) {
                arr[found++] = i * 8 + j;
            }
        }
    }
    free(data);

    return found;
}

static int is_set_bit(uint32_t bm_start_addr, uint32_t bit) {
    uint8_t byte;

    SEEK_BITMAP(bm_start_addr, bit)
    fread(&byte, sizeof(uint8_t), 1, FS->file);
    int bb = BE_BIT(bit);
    return byte & bb;
}

static int set_bit(uint32_t bm_start_addr, uint32_t bit, bool set_to_one) {
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
    VALIDATE(load_file(f = fopen(FS->filename, "rb+")))
    FS->file = f;
    return 0;
}

static uint32_t read_cluster(uint32_t cluster, uint32_t read_amount, uint8_t* byte_arr, uint32_t offset) {
    SEEK_CLUSTER(cluster)

    fseek(FS->file, offset, SEEK_CUR);
    return fread(byte_arr, sizeof(uint8_t), read_amount, FS->file);
}

static int free_cluster(uint32_t cluster_id) {
    uint32_t cluster;
    uint8_t zero[CLUSTER_SIZE] = {0};

    cluster = TO_DATA_CLUSTER(cluster_id);
    SEEK_CLUSTER(cluster)
    fwrite(zero, CLUSTER_SIZE, 1, FS->file);
    fflush(FS->file);
    if (set_bit(FS->sb->data_bm_start_addr, cluster_id, false)) {
        FS->sb->free_cluster_count++;
    }
    return 0;
}

static int
write_cluster(uint32_t cluster_id, void* ptr, uint32_t size, uint32_t n, uint32_t offset, bool as_data_cluster,
              bool override) {
    uint8_t arr[CLUSTER_SIZE];
    unsigned long read;
    uint32_t cluster;

    cluster = cluster_id;
    if (as_data_cluster) {
        cluster = TO_DATA_CLUSTER(cluster);
    }

    if (!override) {
        read = read_cluster(cluster, offset, arr, 0);
        if (read + size * n > CLUSTER_SIZE) {
            return 1;
        }

        // append the previous data
        memcpy(arr + read, ptr, size * n);
    }
    // write the data
    SEEK_CLUSTER(cluster)
    fwrite(ptr, size, n, FS->file);

    // mark the cluster ID as used
    if (as_data_cluster) {
        if (set_bit(FS->sb->data_bm_start_addr, cluster_id, true)) {
            FS->sb->free_cluster_count--;
        }
    }

    // flush the data at the end
    fflush(FS->file);
    return 0;
}

static int init_superblock(uint32_t disk_size, struct superblock** _sb) {
    // [superblock | data block bitmap | inode bitmap | inode table | data used_clusters]
    struct superblock* sb;
    uint32_t used_clusters;
    uint32_t inode_bm_size;
    uint32_t inode_block_size;
    uint32_t data_bm_size;

    sb = malloc(sizeof(struct superblock));
    VALIDATE_MALLOC(sb)

    strcpy(sb->signature, SIGNATURE);
    sb->cluster_size = CLUSTER_SIZE;
    sb->inode_size = sizeof(struct inode);
    sb->disk_size = disk_size;
    ALIGN_DOWN(sb->disk_size, sb->cluster_size)
    sb->cluster_count = sb->disk_size / sb->cluster_size;
    sb->inode_count = (uint32_t) (sb->cluster_count * INODE_PER_CLUSTER);
    ALIGN_UP(sb->inode_count, sb->cluster_size / sb->inode_size) // align to a cluster
    sb->free_inode_count = sb->inode_count;

    inode_bm_size = ALIGNED_UP(sb->inode_count / 8, sb->cluster_size); // the inode bitmap size in bytes
    inode_block_size = sb->inode_count * sb->inode_size;                     // the inode block size in bytes
    data_bm_size = ALIGNED_UP(ALIGNED_UP(sb->cluster_count, 8) / 8, sb->cluster_size);

    used_clusters = 1; // superblock
    used_clusters += inode_bm_size / sb->cluster_size;
    used_clusters += inode_block_size / sb->cluster_size;
    used_clusters += data_bm_size / sb->cluster_size;
    sb->free_cluster_count = sb->cluster_count - used_clusters;

    sb->data_bm_start_addr = sb->cluster_size;
    sb->inode_bm_start_addr = sb->data_bm_start_addr + data_bm_size;
    sb->inode_start_addr = sb->inode_bm_start_addr + inode_bm_size;
    sb->data_start_addr = sb->inode_start_addr + inode_block_size;
    sb->first_ino = FIRST_FREE_INODE;

    *_sb = sb;
    return 0;
}

static int write_fs_header() {

    // write the superblock
    VALIDATE(write_cluster(0, FS->sb, sizeof(struct superblock), 1, 0, false, true))
    return 0;
}

static uint32_t create_inode() {
    struct inode* inode;
    uint32_t inode_id;
    uint32_t amount;

    VALIDATE_MALLOC(inode = malloc(sizeof(struct inode)))
    amount = find_free_spots(FS->sb->inode_bm_start_addr, FS->sb->inode_start_addr, 1, &inode_id);
    if (amount <= 0) {
        fprintf(stderr, "Ran out of inode_ptr space!\n");
        return 0;
    }

    inode->id = inode_id + 1;
    inode->file_size = 0;
    inode->file_type = FILE_TYPE_UNKNOWN;
    inode->hard_links = 0;
    memset(inode->direct, 0, sizeof(inode->direct));
    memset(inode->indirect, 0, sizeof(inode->indirect));
    if (set_bit(FS->sb->inode_bm_start_addr, inode_id, true)) {
        FS->sb->free_inode_count--;
    } else {
        fprintf(stderr, "Created an inode with an ID that already exists!\n");
        return 0;
    }
    if (write_inode_to_disk(inode)) {
        fprintf(stderr, "Could not write an inode with ID %u to the disk!\n", inode->id);
        return 0;
    }

    return inode->id;
}

static struct inode* read_inode_from_disk(uint32_t inode_id) {
    struct inode* inode;
    if (inode_id > FS->sb->inode_count || !is_set_bit(FS->sb->inode_bm_start_addr, inode_id - 1)) {
        fprintf(stderr, "No inode with id %u exists!\n", inode_id);
        return NULL;
    }

    inode = malloc(sizeof(struct inode));
    if (!inode) {
        return NULL;
    }
    SEEK_INODE(inode_id)
    fread(inode, FS->sb->inode_size, 1, FS->file);
    return inode;
}

static int write_inode_to_disk(struct inode* inode) {
    if (!inode) {
        return 1;
    }

    SEEK_INODE(inode->id)
    fwrite(inode, FS->sb->inode_size, 1, FS->file);
    fflush(FS->file);

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
    VALIDATE(create_inode())
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
    VALIDATE(create_inode())
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

int fs_format(uint32_t disk_size) {
    FILE* f;
    struct superblock* sb;
    uint8_t arr[CLUSTER_SIZE];


    VALIDATE(init_superblock(disk_size, &sb))
    FS->sb = sb;
    VALIDATE(load_file(f = fopen(FS->filename, "wb+")))
    ftruncate(fileno(f), sb->disk_size); // sets the file to the size
    VALIDATE(write_fs_header())
    return 0;
}

void test() {
    int i;
    struct inode* inode;
    uint32_t a;
    uint8_t arr[CLUSTER_SIZE];

    fs_format(500 * 1024 * 1024 + 10);

    a = 140234005;
    //write_cluster(0, &a, sizeof(uint32_t), 1, 0, true, true);

    for (i = 0; i < 8; ++i) {
        inode = read_inode_from_disk(create_inode());
        inode->file_size = 1000 * i + 1000;
        write_inode_to_disk(inode);
    }

    inode = read_inode_from_disk(5);

}
