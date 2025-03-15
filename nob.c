// nob.c
#define NOB_IMPLEMENTATION
#include "nob.h"
#include <stdio.h>

#ifndef _WIN32
#include <dirent.h>
#endif 

#define CFLAGS "-Wall", "-Wextra", "-Wswitch-enum", "-std=c11", "-pedantic", "-ggdb"
// #define RUN_FILE "csv/stress-copy.csv"
// #define RUN_FILE "csv/error.csv"
#define RUN_FILE "csv/input.csv"

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
            nob_cmd_append(&run, EXECUTABLE_NAME, RUN_FILE);
            if (!nob_cmd_run_sync(run)) return 1;
        } else if(strcmp(argv[1], "lldb") == 0) {
            Nob_Cmd lldb = {0};
            nob_cmd_append(&lldb, "lldb", "./excel-cli");
            if (!nob_cmd_run_sync(lldb)) return 1;
        } 
#ifndef _WIN32
        else if (strcmp(argv[1], "run-all") == 0) {
            DIR *dir;
            dir = opendir("./csv");
            if(dir) {
                struct dirent *cur_dir;
                while ((cur_dir = readdir(dir)) != NULL) {
                    if(strcmp(cur_dir->d_name, ".") == 0 || strcmp(cur_dir->d_name, "..") == 0) continue;
                    Nob_Cmd run_csv = {0};
                    char *input = malloc(strlen(cur_dir->d_name) + 5);

                    strcpy(input, "csv/");
                    strcat(input, cur_dir->d_name);

                    nob_cmd_append(&run_csv, EXECUTABLE_NAME, input);
                    nob_log(NOB_INFO, "Running a %s as an input", input);
                    if (!nob_cmd_run_sync(run_csv)) nob_log(NOB_ERROR, "Error occured while running %s", cur_dir->d_name);
                }
            } else {
                nob_log(NOB_ERROR, "Can't find a 'csv' folder");
            }

            closedir(dir);
        } 
#endif
        else {
            nob_log(NOB_ERROR, "%s is an unknown suncommand", argv[1]);
        }
    }

    return 0;
}