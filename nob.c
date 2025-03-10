// nob.c
#define NOB_IMPLEMENTATION
#include "nob.h"

#define CFLAGS "-Wall", "-Wextra", "-std=c11", "-pedantic", "-ggdb"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cc", CFLAGS, "-o", "excel-cli", "src/main.c");
    if (!nob_cmd_run_sync(cmd)) return 1;

    if(argc > 1) {
        if(strcmp(argv[1], "run") == 0) {
            Nob_Cmd run = {0};
            nob_cmd_append(&run, "./excel-cli", "input.csv");
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