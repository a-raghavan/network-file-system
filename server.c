#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "udp.h"
#include "mfs.h"
#include "ufs.h"

int sockd;          // socket descriptor
int fsd;            // file system descriptor

super_t *fs;        // The filesystem

void intHandler(int dummy) {
    UDP_Close(sockd);
    close(fsd);
    exit(130);
}

void packResponse(RPC_Response_t *res, enum ErrorCode ec, int inum, MFS_Stat_t *stat, int nbytes, unsigned char *data)
{
    res->errorCode = ec;
    res->inum = inum;
    res->stat.type = !stat ? MFS_UNDEFINED : stat->type;
    res->stat.size = !stat ? -1 : stat->size;
    res->nbytes = nbytes;
    res->checksum = 0;
    memcpy(res->data, data, nbytes);
    return;
}

void lookupHandler(RPC_Request_t *req, RPC_Response_t *res)
{
    int pinum = req->inum;
    char name[28];
    memcpy(name, req->name, 28);

    inode_t *inode = (inode_t *)((char *)fs + ((fs->inode_region_addr*UFS_BLOCK_SIZE) + (32*pinum)));
    printf("fs = %p, inode type = %d\n", fs, inode->size);
    dir_ent_t *entry =  (dir_ent_t *)((char *)fs + inode->direct[0]*UFS_BLOCK_SIZE);
    printf("fs = %p, direct = %d\n", fs, inode->direct[0]);
    int inum = entry->inum;

    packResponse(res, kSuccess, inum, NULL, 0, NULL);
}

// server code
int main(int argc, char *argv[]) {
    if (argc != 3)
        exit(EXIT_FAILURE);

    // garbage collection on SIGINT
    signal(SIGINT, intHandler);
    
    // open udp port
    sockd = UDP_Open(atoi(argv[1]));
    assert(sockd > -1);

    // open FS image
    struct stat imagestat;
    fsd = open (argv[2], O_RDWR);
    fstat (fsd, &imagestat);
    int imagesz = imagestat.st_size;
    printf("image size = %d\n", imagesz);
    fs = (super_t *) mmap(0, imagesz, PROT_READ|PROT_WRITE, MAP_PRIVATE, fsd, 0);

    // start accepting requests from clients
    while (1) {
        RPC_Response_t res;
        struct sockaddr_in addr;
        RPC_Request_t req;

        // Wait for request
        int rc = UDP_Read(sockd, &addr, (char *)&req, sizeof(req));

        // Verify checksum
        // word16 cksm = req.checksum;
        // req.checksum = 0;
        // word16 calc_cksm = UDP_Checksum((byte *)&req, sizeof(req));
        // if (calc_cksm != cksm)
        // {
        //     packResponse(&res, kErrorChecksumFailed, -1, NULL, 0, NULL);
        //     goto sendresponse;
        // }
        
        switch (req.op)
        {
            case kInit:
                // nothing to do
                break;
            case kLookup:
                lookupHandler(&req, &res);
                break;

            case kStat:

            case kWrite:

            case kRead:

            case kCreat:

            case kUnlink:

            case kShutdown:

            default:
                packResponse(&res, kSuccess, 0, NULL, 0, NULL);

        }

        sendresponse: 
        rc = UDP_Write(sockd, &addr, (char *)&res, sizeof(res));

        printf("server :: sent response\n");
    }

    return 0; 
}
    


 
