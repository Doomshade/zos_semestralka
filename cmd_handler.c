#include "cmd_handler.h"
#include "cmds.h"
#include <string.h>
#include <stdio.h>

#define CMD_COUNT 16

typedef int handle(char* args[]);

typedef struct {
    handle* handle;
    const char* cmd_name;
    int argc;
    const char* help;
} CMD;

static CMD cmds[CMD_COUNT];

void print_cmds() {
    int i;
    CMD c;

    for (i = 0; i < CMD_COUNT; ++i) {
        c = cmds[i];
        printf("%s%s(%d)\t-- %s\n", c.cmd_name, (strlen(c.cmd_name) < 4 ? "\t\t" : "\t"), c.argc, c.help);
    }
}

void print_cmd(char* cmd) {
    int i;
    CMD c;

    for (i = 0; i < CMD_COUNT; ++i) {
        c = cmds[i];
        if (strcmp(c.cmd_name, cmd) == 0) {
            printf("%s%s(%d)\t-- %s\n", c.cmd_name, (strlen(c.cmd_name) < 4 ? "\t\t" : "\t"), c.argc, c.help);
            return;
        }
    }
}

static void register_cmd(handle* h, const char* cmd, int argc, const char* help, int idx) {

    CMD c = {h, cmd, argc, help};
    cmds[idx] = c;
}

void register_handlers() {
    int idx = 0;

    register_cmd(cp, "cp", 2, "Zkopíruje soubor s1 do umístění s2", idx++);
    register_cmd(mv, "mv", 2, "Přesune soubor s1 do umístění s2, nebo přejmenuje s1 na s2", idx++);
    register_cmd(rm, "rm", 1, "Smaže soubor s1", idx++);
    register_cmd(mkdir, "mkdir", 1, "Vytvoří adresář a1", idx++);
    register_cmd(rmdir, "rmdir", 1, "Smaže prázdný adresář a1", idx++);
    register_cmd(ls, "ls", 1, "Vypíše obsah adresáře a1", idx++);
    register_cmd(cat, "cat", 1, "Vypíše obsah souboru s1", idx++);
    register_cmd(cd, "cd", 1, "Změní aktuální cestu do adresáře a1", idx++);
    register_cmd(pwd, "pwd", 0, "Vypíše aktuální cestu", idx++);
    register_cmd(info, "info", 1, "Vypíše informace o souboru/adresáři s1/a1 (v jakých clusterech se nachází)", idx++);
    register_cmd(incp, "incp", 2, "Nahraje soubor s1 z pevného disku do umístění s2 ve vašem FS", idx++);
    register_cmd(outcp, "outcp", 2, "Nahraje soubor s1 z vašeho FS do umístění s2 na pevném disku", idx++);
    register_cmd(load, "load", 1,
                 "Načte soubor z pevného disku, ve kterém budou jednotlivé příkazy, a začne je sekvenčně\n"
                 "vykonávat. Formát je 1 příkaz/1řádek", idx++);
    register_cmd(format, "format", 1,
                 "Příkaz provede formát souboru, který byl zadán jako parametr při spuštení programu na\n"
                 "souborový systém dané velikosti. Pokud už soubor nějaká data obsahoval, budou přemazána.\n"
                 "Pokud soubor neexistoval, bude vytvořen.", idx++);
    register_cmd(xcp, "xcp", 3, "Vytvoří soubor s3, který bude spojením souborů s1 a s2.", idx++);
    register_cmd(short_cmd, "short", 1, "Pokud je s1 větší než 5000 bytů, zkrátí jej na 5000 bytů", idx++);
}

int handle_cmd(char* cmd, int argc, char* argv[]) {
    int i;

    if (!cmd || !argv) {
        return CMD_NOT_FOUND;
    }

    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "") == 0) {
            return INVALID_ARG_AMOUNT;
        }
    }

    for (i = 0; i < CMD_COUNT; ++i) {
        CMD c = cmds[i];
        if (strcmp(cmd, c.cmd_name) == 0) {
            if (c.argc != argc) {
                return INVALID_ARG_AMOUNT;
            }
            if (c.handle != format && c.handle != load && !FS->fmt) {
                return FS_NOT_YET_FORMATTED;
            }
            return c.handle(argv);
        }
    }
    return CMD_NOT_FOUND;
}
