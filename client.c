#include <stdio.h>
#include "mfs.h"

// client code
int main(int argc, char *argv[]) {
    char hostname[] = "localhost";

    MFS_Init(hostname, 10004);
    char name[28] = "..";
    int inum = MFS_Lookup(0, name);
    printf("inum = %d\n", inum);

    MFS_Stat_t stat;
    MFS_Stat(0, &stat);
    printf("type %d - size %d\n", stat.type, stat.size);

    MFS_DirEnt_t dir;
    int rc = MFS_Read(0, (char *)&dir, 32, 32);
   
    printf("READ: rc - %d name - %s inum %d \n", rc, dir.name, dir.inum);

    rc = MFS_Creat(0, MFS_DIRECTORY, "asketdir");
    printf("CREATE: rc - %d\n", rc);
    inum = MFS_Lookup(0, "asketdir");

    rc = MFS_Creat(inum, MFS_REGULAR_FILE, "asketfile");
    printf("CREATE: rc - %d\n", rc);

    rc = MFS_Unlink(0, "asketdir");
    printf("UNLINK: rc - %d\n", rc);
    inum = MFS_Lookup(0, "asketdir");
    printf("LOOKUP: inum = %d\n", inum);

    rc = MFS_Unlink(inum, "asketfile");
    printf("UNLINK: rc - %d\n", rc);

    rc = MFS_Unlink(0, "asketdir");
    printf("UNLINK: rc - %d\n", rc);


    // char *buffer = "asketagarwal";
    //     rc = MFS_Write(inum, buffer, 0, 13);
    // printf("Write return code: %d\n", rc);

    // char data[128];
    // rc = MFS_Read(inum, data, 0, 12);
    // printf("Data Read: %s\n", data);

    MFS_Shutdown();

    return 0;
}

