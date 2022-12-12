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
    memcpy(res->data, data, nbytes);
    return;
}

inode_t *inodeEntryAddress(int inum)
{
    return (inode_t *)((char *)fs + ((fs->inode_region_addr*UFS_BLOCK_SIZE) + (sizeof(inode_t)*inum)));
}

 char *dataBlockAddress(int offset)
{
    return (char *)fs + offset*UFS_BLOCK_SIZE;
}

void lookupHandler(RPC_Request_t *req, RPC_Response_t *res)
{
    // TODO: Handle Invalid pinum case
    //      1. pinum > max number of Inodes
    //      2. pinum is not allocated (inode bitmap corresponding to pinum is not set)
    //      3. pinum < 0 ?
    int pinum = req->inum;
    char name[28];
    memcpy(name, req->name, 28);

    // find inode entry in itable  
    inode_t *inode = inodeEntryAddress(pinum);
    
    // walk through file system to check if name exists in parent directory (pinum)
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
        // check all data blocks of parent inode and all directory entries in each data block for 'name'
        dir_ent_t *data_blk =  (dir_ent_t *)dataBlockAddress(inode->direct[i]);
        for (int j = 0; j < UFS_BLOCK_SIZE/sizeof(dir_ent_t); j++)
        {
            dir_ent_t *entry = data_blk + j;
            if (strcmp(entry->name, name) == 0)
            {
                packResponse(res, kSuccess, entry->inum, NULL, 0, NULL);
                return;
            }
        }
    }
    packResponse(res, kErrorObjectDoesNotExist, -1, NULL, 0, NULL);
    return;
}

void statHandler(RPC_Request_t *req, RPC_Response_t *res)
{
    // TODO: Handle Invalid inum case
    int inum = req->inum;

    // find inode entry in itable
    inode_t *inode = inodeEntryAddress(inum);
    MFS_Stat_t stat;
    stat.size = inode->size;
    stat.type = inode->type;

    packResponse(res, kSuccess, -1, &stat, 0, NULL);
    return;
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
    fs = (super_t *) mmap(0, imagesz, PROT_READ|PROT_WRITE, MAP_PRIVATE, fsd, 0);

    // start accepting requests from clients
    while (1) {
        RPC_Response_t res;
        struct sockaddr_in addr;
        RPC_Request_t req;

        // Wait for request
        int rc = UDP_Read(sockd, &addr, (char *)&req, sizeof(req));

        // Verify checksum
        word16 cksm = req.checksum;
        req.checksum = 0;
        word16 calc_cksm = UDP_Checksum((byte *)&req, sizeof(req));
        if (calc_cksm != cksm)
        {
            packResponse(&res, kErrorChecksumFailed, -1, NULL, 0, NULL);
            goto sendresponse;
        }
        
        switch (req.op)
        {
            case kInit:
                // nothing to do
                break;
            case kLookup:
                lookupHandler(&req, &res);
                break;

            case kStat:
                statHandler(&req, &res);
                break;

            case kWrite:

            case kRead:

            case kCreat:

            case kUnlink:

            case kShutdown:
                fsync(fsd);
                
            default:
                packResponse(&res, kSuccess, 0, NULL, 0, NULL);

        }

        sendresponse: 
        rc = UDP_Write(sockd, &addr, (char *)&res, sizeof(res));

        if (req.op == kShutdown)
            exit(0);

        printf("server :: sent response\n");
    }

    return 0; 
}
    


 
