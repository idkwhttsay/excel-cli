// nob.c
#define NOB_IMPLEMENTATION
#include "nob.h"
#include <stdio.h>

#ifndef _WIN32
#include <dirent.h>
#endif 

#define CFLAGS "-Wall", "-Wextra", "-Wswitch-enum", "-std=c11", "-pedantic", "-ggdb"

// #define IN_FILE "input/stress-copy.csv"
#define IN_FILE "input/large.csv"
// #define IN_FILE "input/ops.csv"
// #define IN_FILE "input/bills.csv"
// #define IN_FILE "input/input.csv"

#define OUT_FILE "out/out.csv"

#ifdef _WIN32
#define BINARY_NAME "excel-cli.exe"
#else
#define BINARY_NAME "excel-cli"
#endif

#ifdef _WIN32
#define EXECUTABLE_NAME "./excel-cli.exe"
#else
#define EXECUTABLE_NAME "./excel-cli"
#endif

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    Nob_Cmd cmd = {0};

#ifdef _WIN32
    nob_cmd_append(&cmd, "gcc", CFLAGS, "-o", BINARY_NAME, "src/main.c");
#else
    nob_cmd_append(&cmd, "cc", CFLAGS, "-o", BINARY_NAME, "src/main.c");
#endif

    if (!nob_cmd_run_sync(cmd)) return 1;

    if(argc > 1) {
        if(strcmp(argv[1], "run") == 0) {
            Nob_Cmd run = {0};
            nob_cmd_append(&run, EXECUTABLE_NAME, IN_FILE, OUT_FILE);
            if (!nob_cmd_run_sync(run)) return 1;
        } else if(strcmp(argv[1], "lldb") == 0) {
            Nob_Cmd lldb = {0};
            nob_cmd_append(&lldb, "lldb", "./excel-cli");
            if (!nob_cmd_run_sync(lldb)) return 1;
        } else {
            nob_log(NOB_ERROR, "%s is an unknown suncommand", argv[1]);
        }
    }

    return 0;
}