#include "fs.h"
#include "util.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint-gcc.h>
#include <unistd.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-constant-out-of-range-compare"
#define SIGNATURE "jsmahy"
#define FIRST_FREE_INODE 1 // the first free inode, leave it at 1 as we don't need any special inodes
#define INODE_PER_CLUSTER ((double) 1 / 4) // inode per cluster ratio

// aligns the number to the "to" value
// e.g. num = 7000, to = 4096 -> this aligns to 8192
#define ALIGNED_UP(num, to) ((num) + ((to) - (num)) % (to))
#define ALIGNED_DOWN(num, to) ((num) % (to) == 0 ? (num) : ALIGNED_UP((num), (to)) - (to))
#define ALIGN_UP(num, to) (num) = ALIGNED_UP((num), (to));
#define ALIGN_DOWN(num, to) (num) = ALIGNED_DOWN((num), (to));
#define SEEK_CLUSTER(cluster) fseek(FS->file, (cluster) * FS->sb->cluster_size, SEEK_SET);
#define TO_DATA_CLUSTER(cluster) ((cluster) + FS->sb->data_start_addr / FS->sb->cluster_size)
#define CAN_ADD_ENTRY_TO_DIR(dir, entry_name) ((dir)->file_type == FILE_TYPE_DIRECTORY && !dir_has_entry((dir), (entry_name)))


#define BE_BIT(bit) (1 << (7 - ((bit) % 8)))

// seek at the start of the bitmap
// then seek to the byte that contains the bit
#define SEEK_BITMAP(bm_start_addr, bit) \
fseek(FS->file, (bm_start_addr), SEEK_SET); \
fseek(FS->file, (bit) / 8, SEEK_CUR); \

#define SEEK_INODE(inode_id) fseek(FS->file, FS->sb->inode_start_addr, SEEK_SET); \
fseek(FS->file, (inode_id - 1) * FS->sb->inode_size, SEEK_CUR);

/*#define READ_BYTES(start_addr, end_addr, arr) fseek(FS->file, (start_addr), SEEK_SET); \
fread((arr), sizeof(uint8_t), (end_arr) - (start_addr), FS->file);
*/
// the union data_size can be either 1, 2, or 4 bytes long
/*#define VALIDATE_SIZE(size) if (!((size) == 1 || (size) == 2 || (size) == 4)) return 1;

#define malloc0(size) calloc(1, (size))
*/

/**
 * Writes an inode to the disk
 * @param inode the inode
 * @return 0 if the inode was written correctly
 */
static bool write_inode_to_disk(struct inode* inode);

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
static bool free_cluster(uint32_t cluster_id);

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
static bool is_set_bit(uint32_t bm_start_addr, uint32_t bit);

/**
 * Sets the bit in the bitmap to 0 or 1
 * @param bm_start_addr the bitmap start address
 * @param bit the bit #
 * @param set_to_one whether to set the bit to 1
 * @return 1 if the bit was set, 0 if not
 */
static bool set_bit(uint32_t bm_start_addr, uint32_t bit, bool set_to_one);

/**
 * Sets the FS->file if the file exists
 * @param f the file
 * @return 0 if the file was set, 1 otherwise
 */
static bool load_file(FILE* f);

/**
 * Loads the file system from a file with the name
 * @param filename the file name
 * @return
 */
static bool load_fs_from_file(char* filename);

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
static bool
write_cluster(uint32_t cluster_id, void* ptr, uint32_t size, uint32_t n, uint32_t offset, bool as_data_cluster,
              bool override);

/**
 * Initializes the superblock with the given disk size and
 * @param disk_size the disk size
 * @param _sb the superblock pointer
 * @return 0 if successfully initialized
 */
static bool init_superblock(uint32_t disk_size, struct superblock** _sb);

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
    data = malloc(len * sizeof(uint8_t));
    VALIDATE_MALLOC(data)
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

static bool is_set_bit(uint32_t bm_start_addr, uint32_t bit) {
    uint8_t byte;

    SEEK_BITMAP(bm_start_addr, bit)
    fread(&byte, sizeof(uint8_t), 1, FS->file);
    int bb = BE_BIT(bit);
    return byte & bb;
}

static bool set_bit(uint32_t bm_start_addr, uint32_t bit, bool set_to_one) {
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

static bool load_file(FILE* f) {
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

static bool load_fs_from_file(char* filename) {
    FILE* f;
    VALIDATE(load_file(f = fopen(FS->filename, "rb+")))
    FS->file = f;
    return 0;
}

static uint32_t read_cluster(uint32_t cluster, uint32_t read_amount, uint8_t* byte_arr, uint32_t offset) {
    if (cluster == 0) {
        return 0;
    }
    SEEK_CLUSTER(cluster)
    fseek(FS->file, offset, SEEK_CUR);
    return fread(byte_arr, sizeof(uint8_t), read_amount, FS->file);
}

static bool free_cluster(uint32_t cluster_id) {
    uint32_t cluster;
    uint8_t zero[CLUSTER_SIZE] = {0};

    if (IS_FREE_CLUSTER(cluster_id)) {
        return 1;
    }
    cluster = TO_DATA_CLUSTER(cluster_id);
    SEEK_CLUSTER(cluster)
    fwrite(zero, CLUSTER_SIZE, 1, FS->file);
    fflush(FS->file);
    if (set_bit(FS->sb->data_bm_start_addr, cluster_id, false)) {
        FS->sb->free_cluster_count++;
    }
    return 0;
}

static bool
write_cluster(uint32_t cluster_id, void* ptr, uint32_t size, uint32_t n, uint32_t offset, bool as_data_cluster,
              bool override) {
    uint8_t arr[CLUSTER_SIZE];
    unsigned long read;
    uint32_t cluster;

    cluster = cluster_id;
    if (as_data_cluster) {
        // allocate a new cluster
        if (IS_FREE_CLUSTER(cluster_id)) {
            if (!find_free_spots(FS->sb->data_bm_start_addr, FS->sb->inode_bm_start_addr, 1, &cluster_id)) {
                fprintf(stderr, "Ran out of clusters!\n");
                exit(1);
            }
            offset = 0;
        } else {
            // decrement by one as the value expected is > 0, but we index from 0 anyways
            cluster_id--;
        }
        cluster = TO_DATA_CLUSTER(cluster_id);
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
    fflush(FS->file);

    // mark the cluster ID as used
    if (as_data_cluster) {
        if (set_bit(FS->sb->data_bm_start_addr, cluster_id, true)) {
            FS->sb->free_cluster_count--;
        }
    }

    return 0;
}

static bool init_superblock(uint32_t disk_size, struct superblock** _sb) {
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

static bool write_fs_header() {

    // write the superblock
    VALIDATE(write_cluster(0, FS->sb, sizeof(struct superblock), 1, 0, false, true))
    return 0;
}

static bool free_inode(struct inode* inode) {
    return true;
}

static uint32_t create_inode() {
    struct inode* inode;
    uint32_t inode_id;
    uint32_t amount;

    inode = malloc(sizeof(struct inode));
    VALIDATE_MALLOC(inode)
    amount = find_free_spots(FS->sb->inode_bm_start_addr, FS->sb->inode_start_addr, 1, &inode_id);
    if (amount <= 0) {
        fprintf(stderr, "Ran out of inode space!\n");
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
    printf("Created an inode %u\n", inode->id);

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

static bool write_inode_to_disk(struct inode* inode) {
    if (!inode) {
        return 1;
    }

    SEEK_INODE(inode->id)
    fwrite(inode, FS->sb->inode_size, 1, FS->file);
    fflush(FS->file);

    return 0;
}

static uint32_t get_dir_entries(struct inode* dir, struct entry*** _entries) {
    struct entry** entries;
    uint32_t number_of_entries;
    uint32_t remaining_bytes;
    uint32_t index;
    uint32_t i;
    uint32_t max_read;
    struct entry** ptr_copy;

    remaining_bytes = dir->file_size;
    entries = malloc(sizeof(struct entry*) * dir->file_size / sizeof(struct entry));
    ptr_copy = entries;

    index = 0;
    while (remaining_bytes) {
        for (i = 0; i < 5 && remaining_bytes; ++i) {
            max_read = MIN(FS->sb->cluster_size, remaining_bytes);
            //memcpy(ptr_copy, &dir->direct[i], max_read);

            number_of_entries = max_read / sizeof(struct entry);
            ptr_copy += number_of_entries;
            remaining_bytes -= max_read;
        }
    }
    *_entries = entries;
    return dir->file_size / sizeof(struct entry);
}

static bool dir_has_entry(struct inode* dir, char name[MAX_FILENAME_LENGTH]) {
    struct entry** entries;
    uint32_t count;
    uint32_t i;

    count = get_dir_entries(dir, &entries);
    for (i = 0; i < count; ++i) {
        if (entries[i] == NULL) {
            fprintf(stderr, "A null entry encountered, RIP\n");
            continue;
        }
        if (strncmp(name, entries[i]->item_name, MAX_FILENAME_LENGTH) == 0) {
            free(entries);
            return true;
        }
    }
    free(entries);
    return false;
}

static bool get_clusters(int** clusters, uint32_t size) {
    int* _clusters;

    if (!clusters) {
        return 1;
    }

    _clusters = calloc(10, sizeof(int));
    VALIDATE_MALLOC(_clusters)

    *clusters = _clusters;
    return 0;
}

static bool read_data(struct inode* inode, uint32_t* data_len, union data_size** data, uint8_t union_data_size) {
    return 0;
}

static bool write_data(struct inode* inode, void* ptr, uint32_t size, bool append) {
    uint32_t cluster;
    uint32_t offset;
    uint32_t write_size;
    uint32_t i, j;
    uint8_t arr[CLUSTER_SIZE];
    uint32_t indirect_cluster;

    if (inode->file_size + size > MAX_FILE_SIZE) {
        fprintf(stderr, "The inode %u (%u MiB) is too large to save! (max %u MiB)\n", inode->id,
                inode->file_size + size, MAX_FILE_SIZE);
        return false;
    }

    // clear out the allocated clusters if we do not append
    if (!append) {
        for (i = 0; i < DIRECT_AMOUNT; ++i) {
            free_cluster(inode->direct[i]);
        }
        for (i = 0; i < INDIRECT_AMOUNT; ++i) {
            if (read_cluster(inode->indirect[i], FS->sb->cluster_size, arr, 0)) {
                for (j = 0; j < FS->sb->cluster_size / sizeof(uint32_t); ++i) {
                    read_cluster(inode->indirect[i], sizeof(uint32_t), (uint8_t*) &indirect_cluster, 0);
                    free_cluster(indirect_cluster);
                }
            }
        }
    }

    // we can write to direct blocks
    while (size > 0 && inode->file_size < FS->sb->cluster_size * DIRECT_AMOUNT) {
        // write to a cluster with the given offset
        // the offset will be zeroed if the cluster was empty
        cluster = inode->direct[inode->file_size / FS->sb->cluster_size];
        offset = inode->file_size % FS->sb->cluster_size;
        write_size = MIN(size, FS->sb->cluster_size - offset);
        size -= write_size;
        inode->file_size += write_size;

        write_cluster(cluster, ptr, write_size, 1, offset, true, false);
    }
    return write_inode_to_disk(inode);
}

static bool remove_entry(struct inode* dir, char name[MAX_FILENAME_LENGTH]) {
    struct entry** entries;
    uint32_t i;
    uint32_t amount;
    if (!dir_has_entry(dir, name)) {
        return false;
    }

    if (!(amount = get_dir_entries(dir, &entries))) {
        return false;
    }

    for (i = 0; i < amount; ++i) {
        if (strncmp(entries[i]->item_name, name, MAX_FILENAME_LENGTH) == 0) {
            // TODO
            return true;
        }
    }
    return false;
}

static bool add_entry(struct inode* dir, struct entry* entry) {
    if (!dir || !entry || !(CAN_ADD_ENTRY_TO_DIR(dir, entry->item_name))) {
        return false;
    }

    if (read_inode_from_disk(entry->inode_id)->file_type == FILE_TYPE_DIRECTORY) {
        dir->hard_links++;
    }
    write_data(dir, entry, sizeof(struct entry), true);
    return true;
}

static struct entry* create_empty_file(uint32_t dir_inode_id, char name[MAX_FILENAME_LENGTH], struct inode** _inode) {
    struct inode* inode;
    struct inode* dir;
    struct entry* entry;

    dir = read_inode_from_disk(dir_inode_id);
    if (!dir || !CAN_ADD_ENTRY_TO_DIR(dir, name)) {
        fprintf(stderr, "Could not add an entry to a dir %u\n", dir_inode_id);
        return NULL;
    }
    entry = malloc(sizeof(struct entry));
    strncpy(entry->item_name, name, MAX_FILENAME_LENGTH);

    inode = read_inode_from_disk(create_inode());
    entry->inode_id = inode->id;
    write_inode_to_disk(inode);
    *_inode = inode;
    return entry;
}

static struct entry* __create_dir(uint32_t parent_dir_inode_id, char name[MAX_FILENAME_LENGTH], bool root) {
    struct inode* parent;
    struct inode* dir;
    struct entry* e_self;
    struct entry* e_parent;
    struct entry* entry;

    if (root) {
        if (FS->root != NULL) {
            fprintf(stderr, "Attempted to create a root directory when one already exists!\n");
            return NULL;
        }
        dir = read_inode_from_disk(create_inode());
        parent = dir;
    } else {
        parent = read_inode_from_disk(parent_dir_inode_id);
        if (strchr(name, '/') != NULL) {
            fprintf(stderr, "Invalid name '%s' - it cannot contain '/'!\n", name);
            return NULL;
        }
        if (parent == NULL || !CAN_ADD_ENTRY_TO_DIR(parent, name)) {
            return 0;
        }
        dir = read_inode_from_disk(create_inode());
    }

    e_self = malloc(sizeof(struct entry));
    entry = malloc(sizeof(struct entry));
    e_parent = malloc(sizeof(struct entry));

    dir->file_type = FILE_TYPE_DIRECTORY;

    entry->inode_id = dir->id;
    strncpy(entry->item_name, name, MAX_FILENAME_LENGTH);
    e_self->inode_id = dir->id;
    strcpy(e_self->item_name, ".");
    e_parent->inode_id = parent->id;
    strcpy(e_parent->item_name, "..");

    if (!add_entry(dir, e_self) || !add_entry(dir, e_parent)) {
        return 0;
    }
    // don't add an entry to the parent if this is the root
    if (!root && !add_entry(parent, entry)) {
        return 0;
    }

    return entry;
}

struct entry* create_dir(uint32_t parent_dir_inode_id, char name[MAX_FILENAME_LENGTH]) {
    return __create_dir(parent_dir_inode_id, name, false);
}

struct entry* create_file(uint32_t dir_inode_id, char name[MAX_FILENAME_LENGTH]) {
    struct inode* inode;
    struct entry* entry;

    entry = create_empty_file(dir_inode_id, name, &inode);
    inode->file_type = FILE_TYPE_REGULAR_FILE;
    return entry;
}

int init_fs(char* filename) {
    struct fs* fs;
    FILE* f;

    if (FS) {
        fprintf(stderr, "Already initialized the file system!\n");
        return 1;
    }
    fs = malloc(sizeof(struct fs));
    VALIDATE_MALLOC(fs)

    fs->fmt = false;
    fs->filename = filename;
    fs->root = NULL;
    fs->curr_dir = NULL;
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
    struct entry* root;

    VALIDATE(init_superblock(disk_size, &sb))
    FS->sb = sb;
    VALIDATE(load_file(f = fopen(FS->filename, "wb+")))
    ftruncate(fileno(f), sb->disk_size); // sets the file to the size
    VALIDATE(write_fs_header())

    root = __create_dir(0, "/", true);
    if (!root) {
        return 1;
    }

    FS->root = root;
    FS->curr_dir = root;
    printf("Root: %u %s\n", root->inode_id, root->item_name);
    return 0;
}

void test() {
    int i;
    struct inode* inode;
    struct entry* entry;
    uint32_t a;
    uint8_t arr[CLUSTER_SIZE];

    fs_format(500 * 1024 * 1024 + 10);

    a = 140234005;
    write_cluster(1, &a, sizeof(uint32_t), 1, 0, true, true);

    for (i = 0; i < 10; ++i) {
        entry = create_file(FS->root->inode_id, "test");
        printf("Entry inode: %u\n", entry->inode_id);
    }

    inode = read_inode_from_disk(5);
}

#pragma clang diagnostic pop

