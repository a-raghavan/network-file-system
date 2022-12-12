#include <stdio.h>
#include "mfs.h"

// client code
int main(int argc, char *argv[]) {
    char hostname[] = "localhost";

    MFS_Init(hostname, 10000);
    char name[28] = "..";
    int inum = MFS_Lookup(0, name);
    printf("inum = %d\n", inum);

    MFS_Stat_t stat;
    MFS_Stat(0, &stat);
    printf("type %d - size %d\n", stat.type, stat.size);

    MFS_Shutdown();

    return 0;
}

