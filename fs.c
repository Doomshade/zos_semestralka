#include "fs.h"
#include "util.h"
#include "io.h"
#include <stdlib.h>
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
#define TO_DATA_CLUSTER(cluster) (((cluster) - 1) + FS->sb->data_start_addr / FS->sb->cluster_size)
#define CAN_ADD_ENTRY_TO_DIR(dir, entry_name) ((dir)->file_type == FILE_TYPE_DIRECTORY && !dir_has_entry_((dir), (entry_name)))

#define BE_BIT(bit) (1 << (7 - ((bit) % 8)))

// seek at the start of the bitmap
// then seek to the byte that contains the bit
#define SEEK_BITMAP(bm_start_addr, bit) \
fseek(FS->file, (bm_start_addr), SEEK_SET); \
fseek(FS->file, (bit) / 8, SEEK_CUR); \

#define SEEK_INODE(inode_id) fseek(FS->file, FS->sb->inode_start_addr, SEEK_SET); \
fseek(FS->file, ((inode_id) - 1) * FS->sb->inode_size, SEEK_CUR);

/*#define READ_BYTES(start_addr, end_addr, arr) fseek(FS->file, (start_addr), SEEK_SET); \
fread((arr), sizeof(uint8_t), (end_arr) - (start_addr), FS->file);
*/
// the union data_size can be either 1, 2, or 4 bytes long
/*#define VALIDATE_SIZE(size) if (!((size) == 1 || (size) == 2 || (size) == 4)) return 1;

#define malloc0(size) calloc(1, (size))
*/

/**
 * Creates a directory
 * @param parent
 * @param name
 * @param root
 * @param inode
 * @return
 */
static struct inode* create_empty_dir(struct inode* parent, const char name[MAX_FILENAME_LENGTH], bool root);

/**
 * Writes an inode to the disk
 * @param inode the inode
 * @return 0 if the inode was written correctly
 */
static bool inode_write(struct inode* inode);

/**
 * Reads an inode from the disk
 * @param inode_id the inode id
 * @return the inode or NULL if an inode with that id doesn't exist or is invalid
 */
static struct inode* inode_read(uint32_t inode_id);

/**
 * Creates an inode
 * @return 0 if the inode could not be created, otherwise the inode ID
 */
static struct inode* inode_create();

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
static bool load_fs_from_file(const char* filename);

/**
 * Initializes the superblock with the given disk size and
 * @param disk_size the disk size
 * @param _sb the superblock pointer
 * @return 0 if successfully initialized
 */
static bool init_superblock(uint32_t disk_size, struct superblock** _sb);


/**
 * Reads an indirect cluster or a direct one if rank 0 is passed
 * @param cluster cluster #
 * @param arr the array buffer
 * @param size the size of the array buffer
 * @param rank the indirect "rank" 0 = direct, 1 = single indirect, 2 = double indirect
 * @return if the cluster was read correctly
 */
static bool read_indirect_cluster(uint32_t cluster, uint8_t* arr, uint32_t size, uint8_t rank);

static uint64_t write_recursive(uint32_t* cluster, void** ptr, uint64_t size, uint64_t* prev_size, uint8_t rank);

static bool write_fs_header();

static bool free_inode(struct inode* inode);

/**
 * Writes data to a direct cluster
 * @param inode the inode to write to
 * @param ptr the ptr to data
 * @param size the amount to write
 */
static void write_directs(struct inode* inode, void** ptr, uint32_t* size);

/**
 * Writes data to an indirect cluster
 * @param inode the inode to write to
 * @param ptr the ptr to data
 * @param size the amount to write
 */
static void write_indirect1(struct inode* inode, void** ptr, uint32_t* size);

/**
 * Writes data to a second indirect cluster
 * @param inode the inode to write to
 * @param ptr the ptr to data
 * @param size the amount to write
 */
static void write_indirect2(struct inode* inode, void** ptr, uint32_t* size);

static uint32_t
write_inode_cluster(struct inode* inode, void** ptr, uint32_t* size, uint32_t cluster);

/**
 * The file system global variable definition.
 * [superblock | data bitmap | inode bitmap | inodes | data]
 */
struct fs* FS = NULL;

static bool load_file(FILE* f) {
    if (!f) {
        return 1;
    }
    FS->file = f;
    return 0;
}

static bool load_fs_from_file(const char* filename) {
    FILE* f;
    VALIDATE(load_file(f = fopen(FS->filename, "rb+")))
    FS->file = f;
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

    sb->cluster_size = CLUSTER_SIZE;
    sb->disk_size = disk_size;
    if (sb->disk_size < 5 * sb->cluster_size) {
        return false;
    }
    ALIGN_DOWN(sb->disk_size, sb->cluster_size)

    strcpy(sb->signature, SIGNATURE);
    sb->inode_size = sizeof(struct inode);
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
    return true;
}

static bool write_fs_header() {

    // write the superblock
    write_cluster(0, FS->sb, sizeof(struct superblock), 0, false, true);
    return 0;
}

static bool free_inode(struct inode* inode) {
    return true;
}

static struct inode* inode_create() {
    struct inode* inode;
    uint32_t inode_id;

    inode = malloc(sizeof(struct inode));
    VALIDATE_MALLOC(inode)
    if (!find_free_spots(FS->sb->inode_bm_start_addr, FS->sb->inode_start_addr, &inode_id)) {
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
    if (!inode_write(inode)) {
        fprintf(stderr, "Could not write an inode with ID %u to the disk!\n", inode->id);
        return 0;
    }
    return inode;
}

static struct inode* inode_read(uint32_t inode_id) {
    struct inode* inode;
    if (inode_id == FREE_INODE || inode_id > FS->sb->inode_count ||
        !is_set_bit(FS->sb->inode_bm_start_addr, inode_id - 1)) {
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

static bool inode_write(struct inode* inode) {
    if (!inode) {
        return false;
    }

    SEEK_INODE(inode->id)
    fwrite(inode, FS->sb->inode_size, 1, FS->file);
    fflush(FS->file);

    return true;
}

static int compare_entries(const void* aa, const void* bb) {
    struct entry* a = (struct entry*) aa;
    struct entry* b = (struct entry*) bb;
    struct inode* ia = inode_get(a->inode_id);
    struct inode* ib = inode_get(b->inode_id);

    if (!ia || !ib) {
        return 0;
    }

    if (strcmp(a->item_name, CURR_DIR) == 0) {
        return -1;
    }
    if (strcmp(b->item_name, CURR_DIR) == 0) {
        return 1;
    }

    if (strcmp(a->item_name, PREV_DIR) == 0) {
        return -1;
    }

    if (strcmp(b->item_name, PREV_DIR) == 0) {
        return 1;
    }

    // sort by directory
    if (ia->file_type ^ ib->file_type) {
        return ia->file_type == FILE_TYPE_DIRECTORY ? -1 : 1;
    }
    return strcmp(a->item_name, b->item_name);
}

static uint32_t get_dir_entries_(const struct inode* dir, struct entry** _entries) {
    struct entry* entries;
    uint32_t remaining_bytes;
    uint32_t i, j;
    uint32_t max_read;
    uint8_t* read;
    uint32_t index;

    if (!dir) {
        return 0;
    }

    remaining_bytes = dir->file_size;
    entries = malloc(dir->file_size);
    read = malloc(remaining_bytes * sizeof(uint8_t));

    index = 0;
    while (remaining_bytes) {
        for (i = 0; i < 5 && remaining_bytes; ++i) {
            max_read = MIN(FS->sb->cluster_size, remaining_bytes);
            read_cluster(dir->direct[i], max_read, read, 0);
            for (j = 0; j < max_read / sizeof(struct entry); ++j) {
                memcpy(&entries[index++], read + j * sizeof(struct entry), sizeof(struct entry));
                remaining_bytes -= sizeof(struct entry);
            }
        }
    }
    free(read);

    // if an entry ptr is passed the call likely only wanted the amount of entries
    if (_entries) {
        *_entries = entries;
    }
    return dir->file_size / sizeof(struct entry);
}

static bool dir_has_entry_(const struct inode* dir, const char name[MAX_FILENAME_LENGTH]) {
    struct entry* entries;
    uint32_t count;
    uint32_t i;

    if (!dir) {
        return false;
    }
    count = get_dir_entries_(dir, &entries);
    for (i = 0; i < count; ++i) {
        if (entries[i].inode_id == FREE_INODE) {
            fprintf(stderr, "A null entry encountered, RIP\n");
            continue;
        }
        if (strncmp(name, entries[i].item_name, MAX_FILENAME_LENGTH) == 0) {
            free(entries);
            return true;
        }
    }
    free(entries);
    return false;
}

static bool write_data(struct inode* inode, void* ptr, uint32_t size, bool append) {
    uint32_t i;
    uint64_t prev_size;
    if (!inode || !ptr) {
        return false;
    }

    if (!append) {
        inode->file_size = 0;
    }
    prev_size = inode->file_size;
    inode->file_size += size;

    if (inode->file_size > MAX_FS) {
        fprintf(stderr, "The inode %u (%u MiB) is too large to save! (max %lu MiB)\n", inode->id,
                inode->file_size + size, MAX_FS);
        return false;
    }

    for (i = 0; i < DIRECT_AMOUNT; ++i) {
        size -= write_recursive(&inode->direct[i], &ptr, size, &prev_size, 0);
    }

    for (i = 0; i < INDIRECT_AMOUNT; ++i) {
        size -= write_recursive(&inode->indirect[i], &ptr, size, &prev_size, i + 1);
    }
    if (size != 0) {
        inode->file_size = prev_size;
        return false;
    }
    return inode_write(inode);
}

static void write_directs(struct inode* inode, void** ptr, uint32_t* size) {
    uint32_t cluster;

    if (!inode || !ptr || !*ptr || !size || !*size) {
        return;
    }

    while (*size && inode->file_size < DIRECT_TOTAL_SIZE) {
        cluster = inode->file_size / FS->sb->cluster_size;
        inode->direct[cluster] = write_inode_cluster(inode, ptr, size, inode->direct[cluster]);
    }
}

static void write_indirect1(struct inode* inode, void** ptr, uint32_t* size) {
    uint32_t cluster;
    uint32_t offset;
    uint32_t write_size;
    uint32_t id1[CLUSTER_SIZE / sizeof(uint32_t)];

    if (!inode || !ptr || !*ptr || !size || !*size) {
        return;
    }

    memset(id1, 0, sizeof(id1));
    // the indirect cluster didn't exist, write an empty one
    if (!read_cluster(inode->indirect[0], FS->sb->cluster_size, (uint8_t*) id1, 0)) {
        inode->indirect[0] = write_cluster(0, id1, sizeof(id1), 0, true, true);
    }

    // index in indirect cluster = (size - DIRECTS) / cluster_size
    while (*size && inode->file_size < DIRECT_TOTAL_SIZE + INDIRECT1_TOTAL_SIZE) {
        // the index is (file_size - DIRECT_TOTAL_SIZE) / FS->sb->cluster_size
        uint32_t index = (inode->file_size - DIRECT_TOTAL_SIZE) / FS->sb->cluster_size;
        cluster = id1[index];
        offset = inode->file_size % FS->sb->cluster_size;
        write_size = MIN(*size, FS->sb->cluster_size - offset);
        id1[index] = write_cluster(cluster, *ptr, write_size, offset, true, false);

        *size -= write_size;
        inode->file_size += write_size;
        *ptr += write_size;
    }
    write_cluster(inode->indirect[0], id1, sizeof(id1), 0, true, true);
}

static void write_indirect2(struct inode* inode, void** ptr, uint32_t* size) {
    uint32_t cluster;
    uint32_t offset;
    uint32_t write_size;
    uint32_t id1[CLUSTER_SIZE / sizeof(uint32_t)];
    uint32_t id2[CLUSTER_SIZE / sizeof(uint32_t)];


    if (!inode || !ptr || !*ptr || !size || !*size) {
        return;
    }

    read_cluster(inode->indirect[1], FS->sb->cluster_size, (uint8_t*) id2, 0);
    while (*size && inode->file_size < DIRECT_TOTAL_SIZE + INDIRECT1_TOTAL_SIZE + INDIRECT2_TOTAL_SIZE) {
        uint32_t index = (inode->file_size - DIRECT_TOTAL_SIZE) / FS->sb->cluster_size;
        uint32_t index2 = (inode->file_size - DIRECT_TOTAL_SIZE - INDIRECT1_TOTAL_SIZE) /
                          (FS->sb->cluster_size * FS->sb->cluster_size);
        cluster = id2[index2];
        offset = inode->file_size % FS->sb->cluster_size;
        write_size = MIN(*size, FS->sb->cluster_size - offset);
        size -= write_size;
        inode->file_size += write_size;
        id1[index2] = write_cluster(cluster, *ptr, write_size, offset, true, false);
        *ptr += write_size;

    }
}

#define DIRECT0_SIZE (uint64_t) (CLUSTER_SIZE)
#define DIRECT1_SIZE ((CLUSTER_SIZE / 4) * DIRECT0_SIZE)
#define DIRECT2_SIZE ((CLUSTER_SIZE / 4) * DIRECT1_SIZE)

// checks if we can write to a cluster with "max_write_size" and previous size (offset) "prev_size"
// if it's possible, the "size" is decremented by "prev_size" and "prev_size" is set to 0
// if not "prev_size" is decremented by "max_write_size" and 0 is returned
#define VALIDATE_WRITE(size, max_write_size, prev_size) \
if ((*(prev_size)) > (max_write_size)) {\
(*(prev_size)) -= (max_write_size);\
return 0;\
}\
if ((size) + (*(prev_size)) > (max_write_size)) {\
return 0;\
}\


static uint64_t write_recursive(uint32_t* cluster, void** ptr, uint64_t size, uint64_t* prev_size, uint8_t rank) {
    uint32_t buf[CLUSTER_SIZE / 4];
    uint32_t i;
    uint64_t max_write_size;
    uint64_t write_size;
    uint64_t total_write_size;
    uint32_t _cluster;
    if (size == 0 || rank > 2 || !ptr || !*ptr || !prev_size) {
        return 0;
    }

    _cluster = *cluster;
    if (!_cluster) {
        _cluster = write_cluster(0, *ptr, 0, 0, true, true);
        *cluster = _cluster;
    }
    // it's a direct cluster, write the data
    if (rank == 0) {
        VALIDATE_WRITE(size, DIRECT0_SIZE, prev_size)

        write_cluster(_cluster, *ptr, size, *prev_size, true, false);
        *prev_size = 0;
        *ptr += size;

        return size;
    }

    // it's an indirect one
    max_write_size = rank == 1 ? DIRECT1_SIZE : DIRECT2_SIZE;
    VALIDATE_WRITE(size, max_write_size, prev_size)

    read_cluster(_cluster, sizeof(buf), (uint8_t*) buf, 0);
    total_write_size = 0;
    for (i = 0; i < CLUSTER_SIZE / 4 && size; ++i) {
        write_size = write_recursive(&buf[i], ptr, MIN(max_write_size / (CLUSTER_SIZE / 4), size), prev_size, rank - 1);
        size -= write_size;
        total_write_size += write_size;
    }
    write_cluster(_cluster, buf, sizeof(buf), 0, true, true);
    return total_write_size;
}

static uint32_t
write_inode_cluster(struct inode* inode, void** ptr, uint32_t* size, uint32_t cluster) {
    uint32_t offset;
    uint32_t write_size;
    uint32_t cluster_out;

    // calculate the offset and write size
    offset = inode->file_size % FS->sb->cluster_size;
    write_size = MIN(*size, FS->sb->cluster_size - offset);
    *size -= write_size;
    inode->file_size += write_size;

    // write to the cluster and shift the buf pointer
    cluster_out = write_cluster(cluster, *ptr, write_size, offset, true, false);
    *ptr += write_size;
    return cluster_out;
}

static bool remove_entry(struct inode* dir, const char name[MAX_FILENAME_LENGTH]) {
    struct entry* entries;
    uint32_t i;
    uint32_t amount;
    uint8_t* zeroes;

    if (!dir) {
        return false;
    }
    if (strcmp(name, CURR_DIR) == 0 ||
        strcmp(name, PREV_DIR) == 0 ||
        strcmp(name, ROOT_DIR) == 0) {
        fprintf(stderr, "Cannot delete the '%s' entry!\n", name);
        return false;
    }
    if (!dir_has_entry_(dir, name)) {
        return false;
    }

    if (!(amount = get_dir_entries_(dir, &entries))) {
        return false;
    }

    zeroes = malloc(sizeof(struct entry));
    memset(zeroes, 0, sizeof(struct entry));

    for (i = 0; i < amount; ++i) {
        if (strncmp(entries[i].item_name, name, MAX_FILENAME_LENGTH) == 0) {
            // found the entry, remove it and substitute it with the last entry in the list
            // TODO check what cluster it actually is
            write_cluster(dir->direct[0], &entries[amount - 1], sizeof(struct entry),
                          i * sizeof(struct entry), true, false);
            dir->file_size -= sizeof(struct entry);
            return inode_write(dir);
        }
    }
    return false;
}

static bool add_entry(struct inode* dir, struct entry entry) {
    if (!dir || !(CAN_ADD_ENTRY_TO_DIR(dir, entry.item_name))) {
        return false;
    }

    if (inode_read(entry.inode_id)->file_type == FILE_TYPE_DIRECTORY) {
        dir->hard_links++;

    }
    return write_data(dir, &entry, sizeof(struct entry), true);
}

static struct inode*
create_empty_file(uint32_t dir_inode_id, const char name[MAX_FILENAME_LENGTH], uint8_t file_type) {
    struct inode* inode;
    struct inode* dir;
    struct entry entry;

    if (dir_inode_id == FREE_INODE) {
        return NULL;
    }
    dir = inode_read(dir_inode_id);
    if (!dir) {
        return NULL;
    }
    if (!CAN_ADD_ENTRY_TO_DIR(dir, name)) {
        fprintf(stderr, "Could not add an entry to a dir %u\n", dir_inode_id);
        return NULL;
    }

    inode = inode_create();
    inode->file_type = file_type;
    entry = (struct entry) {.inode_id = inode->id};
    strncpy(entry.item_name, name, MAX_FILENAME_LENGTH);
    if (!add_entry(dir, entry)) {
        return NULL;
    }
    return inode_write(inode) ? inode : NULL;
}

struct inode* create_empty_dir(struct inode* parent, const char name[MAX_FILENAME_LENGTH], bool root) {
    struct inode* dir;

    if (root && !parent) {
        if (FS->root != FREE_INODE) {
            fprintf(stderr, "Attempted to create a root directory when one already exists!\n");
            return NULL;
        }
        dir = inode_create();
        parent = dir;
    } else {
        if (strchr(name, '/') != NULL) {
            fprintf(stderr, "Invalid name '%s' - it cannot contain '/'!\n", name);
            return NULL;
        }
        if (!parent || !CAN_ADD_ENTRY_TO_DIR(parent, name)) {
            return NULL;
        }
        dir = inode_create();
    }

    dir->file_type = FILE_TYPE_DIRECTORY;

    struct entry this = {dir->id};
    strncpy(this.item_name, name, MAX_FILENAME_LENGTH);

    struct entry e_self = {dir->id, CURR_DIR};
    struct entry e_parent = {parent->id, PREV_DIR};

    if (!add_entry(dir, e_self) || !add_entry(dir, e_parent)) {
        //free_inode(dir);
        return NULL;
    }
    // don't add an entry to the parent if this is the root
    if (!root && !add_entry(parent, this)) {
        //free_inode(dir);
        return NULL;
    }

    // TODO
    //inode_write(dir);
    return dir;
}


static bool read_indirect(uint32_t cluster, uint8_t* arr, uint32_t* remaining, const uint32_t size, uint8_t rank) {
    if (!arr || !remaining || *remaining <= 0 || cluster == 0 || rank < 0 || rank > 2) {
        return false;
    }
    if (rank == 0) {
        uint32_t offset = (size - *remaining) % FS->sb->cluster_size;
        (*remaining) -= read_cluster(cluster, MIN(*remaining, FS->sb->cluster_size) - offset, (arr + offset), offset);
        return true;
    }

    uint32_t i;
    uint32_t buf[CLUSTER_SIZE / sizeof(uint32_t)];

    read_cluster(cluster, FS->sb->cluster_size, (uint8_t*) buf, 0);
    for (i = 0; i < FS->sb->cluster_size / sizeof(uint32_t); ++i) {
        // once we reach an invalid state (for example cluster ID 0) we stop
        if (!read_indirect(buf[i], arr, remaining, size, rank - 1)) {
            return true;
        }
    }
    return true;
}

static bool read_indirect_cluster(uint32_t cluster, uint8_t* arr, uint32_t size, uint8_t rank) {
    uint32_t remaining = size;
    return read_indirect(cluster, arr, &remaining, size, rank);
}

static void read_directs(struct inode* inode, uint8_t* arr, uint32_t* size) {
    uint32_t i;

    if (!inode || !arr || !size) {
        return;
    }
    for (i = 0; i < 5 && *size; ++i, *size -= MIN(*size, FS->sb->cluster_size)) {
        read_indirect_cluster(inode->direct[i], arr, *size, 0);
    }
}

static void read_indirect1(struct inode* inode, uint8_t* arr, uint32_t* size) {
    const uint32_t _size = *size;
    read_indirect(inode->indirect[0], arr, size, _size, 1);
}


static void read_indirect2(struct inode* inode, uint8_t* arr, uint32_t* size) {
    const uint32_t _size = *size;
    read_indirect(inode->indirect[1], arr, size, _size, 2);
}

uint32_t create_dir(uint32_t parent_dir_inode_id, const char name[MAX_FILENAME_LENGTH]) {
    struct inode* dir = create_empty_dir(inode_read(parent_dir_inode_id), name, false);
    return dir ? dir->id : FREE_INODE;
}

uint32_t create_file(uint32_t dir_inode_id, const char name[MAX_FILENAME_LENGTH]) {
    struct inode* inode = create_empty_file(dir_inode_id, name, FILE_TYPE_REGULAR_FILE);
    return inode ? inode->id : FREE_INODE;
}

int init_fs(char* filename) {
    struct fs* fs;
    if (FS) {
        fprintf(stderr, "Already initialized the file system!\n");
        return 1;
    }
    fs = malloc(sizeof(struct fs));
    VALIDATE_MALLOC(fs)

    fs->fmt = false;
    fs->filename = filename;
    fs->root = FREE_INODE;
    fs->curr_dir = FREE_INODE;
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
    struct inode* root;

    FS->file = NULL;
    FS->root = FREE_INODE;
    FS->curr_dir = FREE_INODE;
    VALIDATE(!init_superblock(disk_size, &sb))
    FS->sb = sb;
    VALIDATE(load_file(f = fopen(FS->filename, "wb+")))
    ftruncate(fileno(f), sb->disk_size); // sets the file to the size
    VALIDATE(write_fs_header())

    root = create_empty_dir(NULL, ROOT_DIR, true);

    if (!root) {
        return 1;
    }
    FS->root = root->id;
    FS->curr_dir = FS->root;
    FS->fmt = true;
    return 0;
}

struct inode* inode_get(uint32_t inode_id) {
    return inode_read(inode_id);
}

bool inode_write_contents(uint32_t inode_id, void* ptr, uint32_t size) {
    struct inode* inode = inode_read(inode_id);
    return inode && inode->file_type == FILE_TYPE_REGULAR_FILE && write_data(inode, ptr, size, false);
}

uint8_t* inode_get_contents(uint32_t inode_id) {
    uint8_t* arr;
    struct inode* inode;
    uint32_t remaining;

    inode = inode_read(inode_id);
    if (!inode) {
        return NULL;
    }
    arr = malloc(inode->file_size);
    VALIDATE_MALLOC(arr)

    remaining = inode->file_size;
    read_directs(inode, arr, &remaining);
    read_indirect1(inode, arr, &remaining);
    read_indirect2(inode, arr, &remaining);
    return !remaining ? arr : NULL;
}


uint32_t inode_from_name(uint32_t dir, const char name[MAX_FILENAME_LENGTH]) {
    struct entry* entries;
    uint32_t i;
    for (i = 0; i < get_dir_entries_(inode_read(dir), &entries); ++i) {
        if (strcmp(entries[i].item_name, name) == 0) {
            return entries[i].inode_id;
        }
    }
    return FREE_INODE;
}

bool dir_has_entry(uint32_t dir, const char name[MAX_FILENAME_LENGTH]) {
    return dir_has_entry_(inode_read(dir), name);
}

uint32_t get_dir_entries(uint32_t dir, struct entry** _entries) {
    return get_dir_entries_(inode_read(dir), _entries);
}

void sort_entries(struct entry** entries, uint32_t size) {
    qsort(*entries, size, sizeof(struct entry), compare_entries);
}

bool get_dir_entry_id(uint32_t dir, struct entry* entry, uint32_t entry_id) {
    struct entry* entries;
    uint32_t i;

    for (i = 0; i < get_dir_entries(dir, &entries); ++i) {
        if (entries[i].inode_id == entry_id) {
            *entry = entries[i];
            return true;
        }
    }
    return false;
}

bool get_dir_entry_name(uint32_t dir, struct entry* entry, const char name[MAX_FILENAME_LENGTH]) {
    struct entry* entries;
    uint32_t i;

    for (i = 0; i < get_dir_entries(dir, &entries); ++i) {
        if (strncmp(entries[i].item_name, name, MAX_FILENAME_LENGTH) == 0) {
            *entry = entries[i];
            return true;
        }
    }
    return false;
}

bool remove_dir(uint32_t dir, const char dir_name[MAX_FILENAME_LENGTH]) {
    struct inode* diri;
    struct entry subdire;


    diri = inode_read(dir);
    // the directory or the subdirectory does not exist
    if (!diri || !get_dir_entry_name(dir, &subdire, dir_name)) {
        return false;
    }

    // the subdirectory has too many entries
    if (get_dir_entries(subdire.inode_id, NULL) != 2) {
        return false;
    }
    return remove_entry(diri, dir_name);
}

void test() {
    int i;
    uint64_t a;
    const char* kuba[] = {"Kuba", "je", "god"};
    struct inode* root;
    uint32_t inode_id;

    fs_format(500 * 1024 * 1024 + 10);

    a = 0xEFCDAB8967452301;
    write_cluster(0, &a, sizeof(a), 0, true, true);

    root = inode_read(FS->root);
    inode_id = create_file(FS->root, "ROFL");
    for (i = 0; i < 3; ++i) {
        struct entry entry = {inode_id};
        strncpy(entry.item_name, kuba[i], MAX_FILENAME_LENGTH);
        add_entry(root, entry);

    }
}

#pragma clang diagnostic pop

