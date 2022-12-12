#include <stdio.h>
#include "mfs.h"

// client code
int main(int argc, char *argv[]) {
    char hostname[] = "localhost";

    MFS_Init(hostname, 10000);
    char name[28] = ".";
    int inum = MFS_Lookup(0, name);
    printf("inum = %d\n", inum);

    return 0;
}

