#ifndef ZOS_SEMESTRALKA_FS_H
#define ZOS_SEMESTRALKA_FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

static const int32_t ID_ITEM_FREE = 0;

typedef int32_t INODE_ID;

struct superblock {
    char signature[9];              //login autora FS
    char volume_descriptor[251];    //popis vygenerovaného FS
    uint32_t disk_size;              //celkova velikost VFS
    uint32_t cluster_size;           //velikost clusteru
    uint32_t cluster_count;          //pocet clusteru
    uint32_t bitmapi_start_address;  //adresa pocatku bitmapy i-uzlů
    uint32_t bitmap_start_address;   //adresa pocatku bitmapy datových bloků
    uint32_t inode_start_address;    //adresa pocatku  i-uzlů
    uint32_t data_start_address;     //adresa pocatku datovych bloku
    uint32_t first_ino;              // first non-reserved inode_id
};


struct inode {
    uint32_t id;                    //ID i-uzlu, pokud ID = ID_ITEM_FREE, je polozka volna
    uint8_t file_type;               //soubor, nebo adresar
    uint8_t references;              //počet odkazů na i-uzel, používá se pro hardlinky
    uint32_t file_size;              //velikost souboru v bytech
    uint32_t direct[5];              // přímé odkazy na datové bloky
    uint32_t indirect[2];
};


struct entry {
    uint32_t inode_id;
    char item_name[12];
};

struct dir {
    struct dir* parent;

    struct entry* this;
    struct entry* subf;
    struct entry* files;
};


struct fs {
    FILE* file;
    struct dir* curr_dir;
};

#endif //ZOS_SEMESTRALKA_FS_H
