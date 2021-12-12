#ifndef ZOS_SEMESTRALKA_CMDS_H
#define ZOS_SEMESTRALKA_CMDS_H

#include <stdint.h>
#include "fs.h"

/**
 * 'Zkopíruje soubor s1 do umístění s2'
 * @param s
 * @return
 */
int cp(char* s[]);

/**
 * 'Přesune soubor s1 do umístění s2, nebo přejmenuje s1 na s2'
 * @param s
 * @return
 */
int mv(char* s[]);

/**
 * 'Smaže soubor s1'
 * @param s
 * @return
 */
int rm(char* s[]);

/**
 * 'Vytvoří adresář a1'
 * @param a
 * @return
 */
int mkdir(char* a[]);

/**
 * 'Smaže prázdný adresář a1'
 * @param a
 * @return
 */
int rmdir(char* a[]);

/**
 * 'Vypíše obsah adresáře a1'
 * @param a
 * @return
 */
int ls(char* a[]);

/**
 * Vypíše obsah souboru s1
 * @param s
 * @return
 */
int cat(char* s[]);

/**
 * 'Změní aktuální cestu do adresáře a1'
 * @param a
 * @return
 */
int cd(char* a[]);

/**
 * 'Vypíše aktuální cestu'
 * @param empt
 * @return
 */
int pwd(char* empt[]);

/**
 * 'Vypíše informace o souboru/adresáři s1/a1 (v jakých clusterech se nachází)'
 * @param as
 * @return
 */
int info(char* as[]);

/**
 * 'Nahraje soubor s1 z pevného disku do umístění s2 ve vašem FS'
 * @param s
 * @return
 */
int incp(char* s[]);

/**
 * 'Nahraje soubor s1 z vašeho FS do umístění s2 na pevném disku'
 * @param s
 * @return
 */
int outcp(char* s[]);

/**
 * 'Načte soubor z pevného disku, ve kterém budou jednotlivé příkazy, a začne je sekvenčně vykonávat. Formát je 1 příkaz/1řádek'
 * @param s
 * @return
 */
int load(char* s[]);

/**
 * 'Příkaz provede formát souboru, který byl zadán jako parametr při spuštení programu na souborový systém dané velikosti.
 * Pokud už soubor nějaká data obsahoval, budou přemazána. Pokud soubor neexistoval, bude vytvořen.'
 * @param s
 * @return
 */
int format(char* s[]);

/**
 * 'Vytvoří soubor s3, který bude spojením souborů s1 a s2.'
 * @param s
 * @return
 */
int xcp(char* s[]);

/**
 * 'Pokud je s1 větší než 5000 bytů, zkrátí jej na 5000 bytů'
 * @param s
 * @return
 */
int short_cmd(char* s[]);

#endif
