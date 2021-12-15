#include "fs.h"
#include "util.h"
#include "io.h"
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
#define TO_DATA_CLUSTER(cluster) (((cluster) - 1) + FS->sb->data_start_addr / FS->sb->cluster_size)
#define CAN_ADD_ENTRY_TO_DIR(dir, entry_name) ((dir)->file_type == FILE_TYPE_DIRECTORY && !dir_has_entry((dir), (entry_name)))


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

static bool write_fs_header();

static bool free_inode(struct inode* inode);

static struct inode* inode_read(uint32_t inode_id);

static bool inode_write(struct inode* inode);

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
    if (inode_write(inode)) {
        fprintf(stderr, "Could not write an inode with ID %u to the disk!\n", inode->id);
        return 0;
    }
    return inode;
}

static struct inode* inode_read(uint32_t inode_id) {
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

static bool inode_write(struct inode* inode) {
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
    uint32_t remaining_bytes;
    uint32_t i, j;
    uint32_t max_read;
    uint8_t* read;
    uint32_t index;
    struct entry* entry;

    remaining_bytes = dir->file_size;
    entries = malloc(sizeof(struct entry*) * dir->file_size / sizeof(struct entry));
    read = malloc(remaining_bytes * sizeof(uint8_t));

    index = 0;
    while (remaining_bytes) {
        for (i = 0; i < 5 && remaining_bytes; ++i) {
            max_read = MIN(FS->sb->cluster_size, remaining_bytes);
            read_cluster(dir->direct[i], max_read, read, 0);
            for (j = 0; j < max_read / sizeof(struct entry); ++j) {
                entry = malloc(sizeof(struct entry));
                memcpy(entry, read + j * sizeof(struct entry), sizeof(struct entry));
                entries[index++] = entry;
                remaining_bytes -= sizeof(struct entry);
            }
        }
    }
    *_entries = entries;
    return dir->file_size / sizeof(struct entry);
}

static bool dir_has_entry(struct inode* dir, const char name[MAX_FILENAME_LENGTH]) {
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

/*static bool read_data(struct inode* inode, uint32_t* data_len, union data_size** data, uint8_t union_data_size) {
    return 0;
}*/

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

    // first write to direct blocks if possible
    while (size > 0 && inode->file_size < FS->sb->cluster_size * DIRECT_AMOUNT) {
        // write to a cluster with the given offset
        // the offset will be zeroed if the cluster was empty
        cluster = inode->direct[inode->file_size / FS->sb->cluster_size];
        offset = inode->file_size % FS->sb->cluster_size;
        write_size = MIN(size, FS->sb->cluster_size - offset);
        size -= write_size;
        inode->file_size += write_size;

        inode->direct[inode->file_size / FS->sb->cluster_size] = write_cluster(cluster, ptr, write_size, offset,
                                                                               true, false);
    }
    return inode_write(inode);
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
        if (!read_indirect(buf[i], arr, remaining, size, rank - 1)) {
            return true;
        }
    }
    return true;
}

static bool read_indirects(uint32_t cluster, uint8_t* arr, const uint32_t size, uint8_t rank) {
    uint32_t remaining = size;
    return read_indirect(cluster, arr, &remaining, size, rank);
}

static bool remove_entry(struct inode* dir, const char name[MAX_FILENAME_LENGTH]) {
    struct entry** entries;
    uint32_t i;
    uint32_t amount;
    uint8_t* zeroes;

    if (strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 ||
        strcmp(name, "/") == 0) {
        fprintf(stderr, "Cannot delete the '%s' entry!\n", name);
        return false;
    }
    if (!dir_has_entry(dir, name)) {
        return false;
    }

    if (!(amount = get_dir_entries(dir, &entries))) {
        return false;
    }

    zeroes = malloc(sizeof(struct entry));
    memset(zeroes, 0, sizeof(struct entry));

    for (i = 0; i < amount; ++i) {
        if (strncmp(entries[i]->item_name, name, MAX_FILENAME_LENGTH) == 0) {
            // found the entry, remove it and substitute it with the last entry in the list
            // TODO check what cluster it actually is
            write_cluster(dir->direct[0], entries[amount - 1], sizeof(struct entry),
                          i * sizeof(struct entry), true, false);
            write_cluster(dir->direct[0], zeroes, sizeof(struct entry),
                          dir->file_size -= sizeof(struct entry), true, false);
            return true;
        }
    }
    return false;
}

static bool add_entry(struct inode* dir, struct entry* entry) {
    if (!dir || entry == NULL || !(CAN_ADD_ENTRY_TO_DIR(dir, entry->item_name))) {
        return false;
    }

    if (inode_read(entry->inode_id)->file_type == FILE_TYPE_DIRECTORY) {
        dir->hard_links++;
    }
    write_data(dir, entry, sizeof(struct entry), true);
    //free(entry);
    return true;
}

static struct entry*
create_empty_file(uint32_t dir_inode_id, const char name[MAX_FILENAME_LENGTH], struct inode** _inode) {
    struct inode* inode;
    struct inode* dir;
    struct entry* entry;

    dir = inode_read(dir_inode_id);
    if (!dir) {
        return NULL;
    }
    if (!CAN_ADD_ENTRY_TO_DIR(dir, name)) {
        fprintf(stderr, "Could not add an entry to a dir %u\n", dir_inode_id);
        return NULL;
    }
    entry = malloc(sizeof(struct entry));
    VALIDATE_MALLOC(entry)
    strncpy(entry->item_name, name, MAX_FILENAME_LENGTH);

    inode = inode_create();
    entry->inode_id = inode->id;
    inode_write(inode);
    *_inode = inode;
    return entry;
}

static struct entry* create_dir_(struct inode* parent, const char name[MAX_FILENAME_LENGTH], bool root) {
    struct inode* dir;
    struct entry* e_self;
    struct entry* e_parent;
    struct entry* entry;

    if (root) {
        if (FS->root != NULL) {
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
        if (parent == NULL || !CAN_ADD_ENTRY_TO_DIR(parent, name)) {
            return NULL;
        }
        dir = inode_create();
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
        return NULL;
    }
    // don't add an entry to the parent if this is the root
    if (!root && !add_entry(parent, entry)) {
        return NULL;
    }

    return entry;
}

struct entry* create_dir(uint32_t parent_dir_inode_id, const char name[MAX_FILENAME_LENGTH]) {
    return create_dir_(inode_read(parent_dir_inode_id), name, false);
}

struct entry* create_file(uint32_t dir_inode_id, const char name[MAX_FILENAME_LENGTH]) {
    struct inode* inode;
    struct entry* entry;

    entry = create_empty_file(dir_inode_id, name, &inode);
    inode->file_type = FILE_TYPE_REGULAR_FILE;
    return entry;
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
    struct entry* root;

    FS->file = NULL;
    FS->root = NULL;
    FS->curr_dir = NULL;
    VALIDATE(!init_superblock(disk_size, &sb))
    FS->sb = sb;
    VALIDATE(load_file(f = fopen(FS->filename, "wb+")))
    ftruncate(fileno(f), sb->disk_size); // sets the file to the size
    VALIDATE(write_fs_header())

    root = create_dir_(NULL, "/", true);
    if (!root) {
        return 1;
    }

    FS->root = root;
    FS->curr_dir = root;
    return 0;
}

void test() {
    int i;
    uint64_t a;
    const char* entry_names[] = {"Kuba", "je", "god"};
    struct inode* root;
    struct entry* entry;

    fs_format(500 * 1024 * 1024 + 10);

    a = 0xEFCDAB8967452301;
    write_cluster(0, &a, sizeof(a), 0, true, true);

    root = inode_read(FS->root->inode_id);
    entry = create_file(FS->root->inode_id, "ROFL");
    for (i = 0; i < 3; ++i) {
        strncpy(entry->item_name, entry_names[i], MAX_FILENAME_LENGTH);
        add_entry(root, entry);
    }
}

#pragma clang diagnostic pop

