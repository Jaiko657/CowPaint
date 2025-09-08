#define NOB_IMPLEMENTATION
#include "third_party/nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};

    // mkdir -p build
    nob_cmd_append(&cmd, "mkdir", "-p", "build");
    if (!nob_cmd_run_sync_and_reset(&cmd)) return 1;

    // compile app
    nob_cmd_append(&cmd,
        "sh", "-lc",
        "cc -std=c99 -Wall -Wextra src/main.c -o build/cowpaint $(pkg-config --cflags --libs raylib)"
    );
    if (!nob_cmd_run_sync_and_reset(&cmd)) return 1;

    return 0;
}
