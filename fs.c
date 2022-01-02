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
#define INODE_PER_CLUSTER ((double) 1 / 4) // inode per cluster ratio

// aligns the number to the "to" value
// e.g. num = 7000, to = 4096 -> this aligns to 8192
#define ALIGNED_UP(num, to) ((num) + ((to) - (num)) % (to))
#define ALIGNED_DOWN(num, to) ((num) % (to) == 0 ? (num) : ALIGNED_UP((num), (to)) - (to))
#define ALIGN_UP(num, to) (num) = ALIGNED_UP((num), (to));
#define ALIGN_DOWN(num, to) (num) = ALIGNED_DOWN((num), (to));
#define CAN_ADD_ENTRY_TO_DIR(dir, entry_name) ((dir)->file_type == FILE_TYPE_DIRECTORY && !dir_has_entry((dir)->id, (entry_name)))

#define SEEK_INODE(inode_id) fseek(FS->file, FS->sb->inode_start_addr, SEEK_SET); \
fseek(FS->file, ((inode_id) - 1) * FS->sb->inode_size, SEEK_CUR);

#define DIRECT0_SIZE (uint64_t) (CLUSTER_SIZE)
#define DIRECT1_SIZE ((CLUSTER_SIZE / 4) * DIRECT0_SIZE)
#define DIRECT2_SIZE ((CLUSTER_SIZE / 4) * DIRECT1_SIZE)
#define MAX_INDIRECT_RANK 2

// checks if we can write to a cluster with "max_write_size" and previous size (offset) "prev_size"
// if it's possible, the "size" is decremented by "prev_size" and "prev_size" is set to 0
// if not "prev_size" is decremented by "max_write_size" and 0 is returned
#define VALIDATE_WRITE(size, max_write_size, prev_size) \
if ((*(prev_size)) > (max_write_size)) {\
(*(prev_size)) -= (max_write_size);\
return 0;\
}

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
static bool load_fs_from_file();

/**
 * Reads the data of an inode to a byte array
 * @param inode
 * @param byte_arr
 * @return
 */
static uint32_t read_data(const struct inode* inode, uint8_t* byte_arr);

/**
 * Initializes the superblock with the given disk size and
 * @param disk_size the disk size
 * @param _sb the superblock pointer
 * @return 0 if successfully initialized
 */
static bool init_superblock(uint32_t disk_size, struct superblock** _sb);

static uint64_t write_recursive(uint32_t* cluster, void** ptr, uint64_t size, uint64_t* prev_size, uint8_t rank);

static bool write_fs_header();

/**
 * Frees an inode and its clusters
 * @param inode the inode
 * @return
 */
static bool inode_free(struct inode* inode);

/**
 * Frees all clusters of an inode
 * @param inode the inode
 */
static void free_clusters(struct inode* inode);

/**
 * The file system global variable definition.
 * [superblock | data bitmap | inode bitmap | inodes | data]
 */
struct fs* FS = NULL;

static bool load_file(FILE* f) {
    if (!f) {
        return false;
    }
    FS->file = f;
    return true;
}

static bool load_fs_from_file() {
    struct superblock* sb;
    uint8_t* arr;

    // open it for read and write as binary
    VALIDATE(!load_file(fopen(FS->filename, "rb+")))

    // read the superblock and set the ptr to that
    arr = read_superblock();
    sb = malloc(sizeof(struct superblock));
    VALIDATE_MALLOC(sb)
    memcpy(sb, arr, sizeof(struct superblock));

    // the inode ID of root will always be 1
    FS->root = 1;
    FS->curr_dir = FS->root;
    FS->sb = sb;
    FS->fmt = true;
    FREE(arr);
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

    memset(sb, 0, sizeof(struct superblock));
    sb->cluster_size = CLUSTER_SIZE;
    sb->disk_size = disk_size;
    if (sb->disk_size < 5 * sb->cluster_size) {
        return false;
    }
    ALIGN_DOWN(sb->disk_size, sb->cluster_size)

    // set the defaults first
    strcpy(sb->signature, SIGNATURE);
    sb->inode_size = sizeof(struct inode);
    sb->cluster_count = sb->disk_size / sb->cluster_size;
    sb->inode_count = (uint32_t) (sb->cluster_count * INODE_PER_CLUSTER);

    // align to a cluster and set the free inode count
    ALIGN_UP(sb->inode_count, sb->cluster_size / sb->inode_size)
    sb->free_inode_count = sb->inode_count;

    // the inode and block bitmap size in bytes
    inode_bm_size = ALIGNED_UP(sb->inode_count / 8, sb->cluster_size);
    inode_block_size = sb->inode_count * sb->inode_size;
    data_bm_size = ALIGNED_UP(ALIGNED_UP(sb->cluster_count, 8) / 8, sb->cluster_size);

    // mark the first clusters as used
    used_clusters = 1; // superblock
    used_clusters += inode_bm_size / sb->cluster_size;
    used_clusters += inode_block_size / sb->cluster_size;
    used_clusters += data_bm_size / sb->cluster_size;
    sb->free_cluster_count = sb->cluster_count - used_clusters;

    // set the start addresses
    sb->data_bm_start_addr = sb->cluster_size;
    sb->inode_bm_start_addr = sb->data_bm_start_addr + data_bm_size;
    sb->inode_start_addr = sb->inode_bm_start_addr + inode_bm_size;
    sb->data_start_addr = sb->inode_start_addr + inode_block_size;

    *_sb = sb;
    return true;
}

static bool write_fs_header() {
    // write the superblock to the cluster #0
    write_cluster(0, FS->sb, sizeof(struct superblock), 0, false, true);
    return true;
}

static bool inode_free(struct inode* inode) {
    // free the clusters before freeing the inode
    free_clusters(inode);
    return free_inode(inode->id);
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
    // set the defaults of an inode
    memset(inode, 0, sizeof(struct inode));
    inode->id = inode_id + 1;
    inode->file_size = 0;
    inode->file_type = FILE_TYPE_UNKNOWN;
    inode->hard_links = 0;
    memset(inode->direct, 0, sizeof(inode->direct));
    memset(inode->indirect, 0, sizeof(inode->indirect));

    // flip a bit in the inode bitmap
    if (set_bit(FS->sb->inode_bm_start_addr, inode_id, true)) {
        FS->sb->free_inode_count--;
    } else {
        fprintf(stderr, "Created an inode with an ID that already exists!\n");
        return NULL;
    }

    // write the inode
    if (!inode_write(inode)) {
        fprintf(stderr, "Could not write an inode with ID %u to the disk!\n", inode->id);
        return NULL;
    }
    write_superblock();
    return inode;
}

static bool inode_write(struct inode* inode) {
    return write_inode(inode->id, (uint8_t*) inode);
}

static uint32_t read_recursive(uint32_t cluster, uint8_t* byte_arr, uint32_t size, uint32_t* curr_read, uint8_t rank) {
    uint32_t i = 0;
    uint32_t buf[CLUSTER_SIZE / sizeof(uint32_t)] = {0};
    uint32_t read = 0;
    uint32_t total_read = 0;

    // check for the cluster and size
    if (cluster == 0 || !byte_arr || rank > MAX_INDIRECT_RANK || size == 0) {
        return 0;
    }

    // if the rank is 0 we read from the cluster
    if (rank == 0) {
        read = read_cluster(cluster, MIN(CLUSTER_SIZE, size), (byte_arr + *curr_read), 0);
        *curr_read += read;
        return read;
    }

    // otherwise it's an indirect cluster, and we read 4 bytes each and call this function again with a rank - 1
    read_cluster(cluster, sizeof(buf), (uint8_t*) buf, 0);
    read = 0;
    total_read = 0;
    for (i = 0; i < CLUSTER_SIZE / 4 && buf[i]; ++i) {
        read = read_recursive(buf[i], byte_arr, size, curr_read, rank - 1);
        total_read += read;
        size -= read;
    }
    return total_read;
}

static uint32_t read_data(const struct inode* inode, uint8_t* byte_arr) {
    uint32_t i = 0;
    uint32_t size = 0;
    uint32_t curr_read = 0;

    size = inode->file_size;

    // first read the directs (rank = 0)
    for (i = 0; i < DIRECT_AMOUNT && size; ++i) {
        size -= read_recursive(inode->direct[i], byte_arr, size, &curr_read, 0);
    }

    // then the indirects (rank = 1, 2)
    for (i = 0; i < INDIRECT_AMOUNT && size; ++i) {
        size -= read_recursive(inode->indirect[i], byte_arr, size, &curr_read, i + 1);
    }
    // if the size is 0, the read was correct
    return size == 0 ? inode->file_size : 0;
}

static void free_cluster_recursive(uint32_t cluster, uint8_t rank) {
    uint32_t i;
    uint32_t arr[CLUSTER_SIZE / sizeof(uint32_t)];

    if (cluster == 0 || rank > MAX_INDIRECT_RANK) {
        return;
    }
    if (rank == 0) {
        free_cluster(cluster);
        return;
    }
    read_cluster(cluster, FS->sb->cluster_size, (uint8_t*) arr, 0);
    for (i = 0; i < FS->sb->cluster_size / sizeof(uint32_t) && arr[i]; ++i) {
        free_cluster_recursive(arr[i], rank - 1);
    }
}

static void free_clusters(struct inode* inode) {
    uint32_t i;
    for (i = 0; i < DIRECT_AMOUNT; ++i) {
        free_cluster(inode->direct[i]);
        inode->direct[i] = 0;
    }

    for (i = 0; i < INDIRECT_AMOUNT; ++i) {
        free_cluster_recursive(inode->indirect[i], i + 1);
        free_cluster(inode->indirect[i]);
        inode->indirect[i] = 0;
    }
}

static bool write_data(struct inode* inode, void* ptr, uint32_t size, bool append) {
    uint32_t i;
    uint64_t prev_size;
    if (!inode || !ptr) {
        return false;
    }

    if (!append) {
        inode->file_size = 0;
        // TODO free all clusters
        free_clusters(inode);
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

static uint64_t write_recursive(uint32_t* cluster, void** ptr, uint64_t size, uint64_t* prev_size, uint8_t rank) {
    uint32_t buf[CLUSTER_SIZE / 4];
    uint32_t i;
    uint64_t max_write_size;
    uint64_t write_size;
    uint64_t total_write_size;
    uint32_t _cluster;
    if (size == 0 || rank > MAX_INDIRECT_RANK || !ptr || !*ptr || !prev_size) {
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

        size = MIN(DIRECT0_SIZE, size);
        write_cluster(_cluster, *ptr, size, *prev_size, true, false);
        *prev_size = 0;
        *ptr += size;

        return size;
    }

    // it's an indirect one
    max_write_size = rank == 1 ? DIRECT1_SIZE : DIRECT2_SIZE;
    VALIDATE_WRITE(size, max_write_size, prev_size)

    size = MIN(size, max_write_size);
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

static bool remove_entry_(struct inode* dir, const char name[MAX_FILENAME_LENGTH]) {
    struct entry* entries = NULL;
    struct inode* inode = NULL;
    uint32_t i = 0;
    uint32_t amount = 0;
    bool success = false;

    uint8_t* arr;
    uint32_t size;

    // validate first
    if (!dir) {
        return false;
    }
    if (strcmp(name, CURR_DIR) == 0 ||
        strcmp(name, PREV_DIR) == 0 ||
        strcmp(name, ROOT_DIR) == 0) {
        fprintf(stderr, "Cannot delete the '%s' entry!\n", name);
        return false;
    }
    if (!dir_has_entry(dir->id, name)) {
        return false;
    }
    if (!(entries = get_dir_entries(dir->id, &amount))) {
        return false;
    }

    // look for the entry and delete the file
    for (i = 0; i < amount; ++i) {
        if (strncmp(entries[i].item_name, name, MAX_FILENAME_LENGTH) == 0) {
            // copy the last "X = sizeof(entry)" bytes (should be 16) to the position found
            // then reduce the size by "X" bytes
            arr = inode_get_contents(dir->id, &size);

            // copy the bytes
            memcpy(arr + i * sizeof(struct entry), arr + (amount - 1) * sizeof(struct entry), sizeof(struct entry));

            // reduce size
            dir->file_size -= sizeof(struct entry);

            // write the data (overwrites everything)
            success = write_data(dir, arr, dir->file_size, false);

            // and then free the inode if the entry was a file
            inode = inode_read(entries[i].inode_id);
            if (!inode) {
                fprintf(stderr, "Could not find an inode with ID %u. This should not happen!\n", entries[i].inode_id);
                return false;
            }
            if (inode->file_type == FILE_TYPE_REGULAR_FILE) {
                if (inode->hard_links <= 1) {
                    inode_free(inode);
                } else {
                    inode->hard_links--;
                }
            }
            FREE(arr)
            FREE(entries);
            return success;
        }
    }
    FREE(entries);
    return false;
}

static bool add_entry_(struct inode* dir, struct entry entry) {
    struct inode* inode;
    if (!dir || !(CAN_ADD_ENTRY_TO_DIR(dir, entry.item_name))) {
        return false;
    }

    inode = inode_read(entry.inode_id);
    if (inode->file_type == FILE_TYPE_DIRECTORY) {
        dir->hard_links++;
    }
    FREE(inode);
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
        FREE(dir);
        return NULL;
    }

    inode = inode_create();
    inode->file_type = file_type;
    inode->hard_links = 1;
    entry = (struct entry) {.inode_id = inode->id};
    strncpy(entry.item_name, name, MAX_FILENAME_LENGTH);
    if (!add_entry_(dir, entry)) {
        FREE(dir);
        return NULL;
    }
    FREE(dir);
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

    if (!add_entry_(dir, e_self) || !add_entry_(dir, e_parent)) {
        //free_inode(dir);
        return NULL;
    }
    // don't add an entry to the parent if this is the root
    if (!root && !add_entry_(parent, this)) {
        //free_inode(dir);
        return NULL;
    }

    // TODO
    //inode_write(dir);
    return dir;
}

struct inode* inode_read(uint32_t inode_id) {
    return (struct inode*) read_inode(inode_id);
}

uint32_t create_dir(uint32_t parent_dir_inode_id, const char name[MAX_FILENAME_LENGTH]) {
    uint32_t id = FREE_INODE;
    struct inode* dir;
    struct inode* parent;

    parent = inode_read(parent_dir_inode_id);
    dir = create_empty_dir(parent, name, false);
    if (parent && dir) {
        id = dir->id;
    }
    FREE(parent);
    FREE(dir);
    return id;
}

uint32_t create_file(uint32_t dir_inode_id, const char name[MAX_FILENAME_LENGTH]) {
    uint32_t id = FREE_INODE;
    struct inode* inode = create_empty_file(dir_inode_id, name, FILE_TYPE_REGULAR_FILE);
    if (inode) {
        id = inode->id;
        FREE(inode);
    }
    return id;
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
        return load_fs_from_file();
    }
    return 0;
}

int fs_format(uint32_t disk_size) {
    struct superblock* sb;
    struct inode* root;

    FS->file = NULL;
    FS->root = FREE_INODE;
    FS->curr_dir = FREE_INODE;
    VALIDATE(!init_superblock(disk_size, &sb))
    FS->sb = sb;

    // create a new file and add rw as binary
    VALIDATE(!load_file(fopen(FS->filename, "wb+")))
    ftruncate(fileno(FS->file), sb->disk_size); // sets the file to the size
    VALIDATE(!write_fs_header())

    root = create_empty_dir(NULL, ROOT_DIR, true);

    if (!root) {
        return 1;
    }
    FS->root = root->id;
    FS->curr_dir = FS->root;
    FS->fmt = true;
    FREE(root);
    return 0;
}

bool inode_write_contents(uint32_t inode_id, void* ptr, uint32_t size) {
    struct inode* inode = inode_read(inode_id);
    bool ret = false;
    if (inode) {
        ret = inode->file_type == FILE_TYPE_REGULAR_FILE && write_data(inode, ptr, size, false);
        FREE(inode);
    }
    return ret;
}

uint8_t* inode_get_contents(uint32_t inode_id, uint32_t* size) {
    uint8_t* arr;
    struct inode* inode;

    inode = inode_read(inode_id);
    if (!inode) {
        return NULL;
    }
    // add +1 for terminating symbol
    arr = malloc(inode->file_size + 1);
    VALIDATE_MALLOC(arr)

    memset(arr, 0, inode->file_size + 1);
    *size = read_data(inode, arr);
    return arr;
}

struct entry* get_dir_entries(uint32_t dir, uint32_t* amount) {
    struct entry* entries = (struct entry*) inode_get_contents(dir, amount);
    *amount /= sizeof(struct entry);
    return entries;
}

bool remove_dir(uint32_t parent, const char dir_name[MAX_FILENAME_LENGTH]) {
    struct inode* diri = NULL;
    struct entry subdire = (struct entry) {.inode_id = FREE_INODE, .item_name = ""};
    struct entry* entries = NULL;
    uint32_t amount = 0;

    diri = inode_read(parent);
    // the directory or the subdirectory does not exist
    if (!diri || !get_dir_entry_name(parent, &subdire, dir_name)) {
        return false;
    }

    // the subdirectory has too many entries
    entries = get_dir_entries(subdire.inode_id, &amount);
    if (amount != 2) {
        FREE(entries);
        return false;
    }
    FREE(entries);
    return remove_entry_(diri, dir_name);
}

bool add_entry(uint32_t parent, struct entry entry) {
    bool added;
    struct inode* inode;

    if (strncmp(entry.item_name, "", 1) == 0 || strstr(entry.item_name, "/") == 0) {
        return false;
    }
    inode = inode_read(parent);
    added = add_entry_(inode, entry);
    FREE(inode);
    return added;
}

bool remove_entry(uint32_t parent, const char entry_name[MAX_FILENAME_LENGTH]) {
    bool removed;
    struct inode* inode;

    inode = inode_read(parent);
    removed = remove_entry_(inode, entry_name);
    FREE(inode);
    return removed;
}

#pragma clang diagnostic pop

