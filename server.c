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

int isInumValid(int inum)
{
    int nInodes = fs->inode_region_len*UFS_BLOCK_SIZE/sizeof(inode_t);
    if (inum < 0 || inum >= nInodes)
        return -1;
    unsigned char *inodeBitmapAddr = (unsigned char *)fs + fs->inode_bitmap_addr*UFS_BLOCK_SIZE;
    unsigned char *t = inodeBitmapAddr + (int)(inum/8);
    unsigned char mask = (1<<(8-inum%8));
    if ((*t & mask) == mask)
        return 0;
    return -1;
}

void lookupHandler(RPC_Request_t *req, RPC_Response_t *res)
{
    int pinum = req->inum;
    char name[28];
    memcpy(name, req->name, 28);

    // find inode entry in itable  
    inode_t *inode = inodeEntryAddress(pinum);

    int numBlocks = inode->size/UFS_BLOCK_SIZE + 1;
    
    // walk through file system to check if name exists in parent directory (pinum)
    for (int i = 0; i < numBlocks; i++)
    {
        // check all data blocks of parent inode and all directory entries in each data block for 'name'
        dir_ent_t *data_blk = (dir_ent_t *)dataBlockAddress(inode->direct[i]);

        int numEntries = (i == numBlocks-1) ? (inode->size%UFS_BLOCK_SIZE)/sizeof(dir_ent_t) : UFS_BLOCK_SIZE/sizeof(dir_ent_t);
        for (int j = 0; j < numEntries; j++)
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
    int inum = req->inum;

    // find inode entry in itable
    inode_t *inode = inodeEntryAddress(inum);
    MFS_Stat_t stat;
    stat.size = inode->size;
    stat.type = inode->type;

    packResponse(res, kSuccess, -1, &stat, 0, NULL);
    return;
}

void readHandler(RPC_Request_t *req, RPC_Response_t *res)
{
    int inum = req->inum;
    int offset = req->offset;
    int nbytes = req->nbytes;

    inode_t *inode = inodeEntryAddress(inum);

    if ( (inode->type == MFS_DIRECTORY && offset % 32 != 0) || offset >= inode->size)
    {
        packResponse(res, kErrorInvalidOffset, -1, NULL, 0, NULL);
        return;
    }

    if (offset + nbytes > inode->size)
    {
        packResponse(res, kErrorInvalidNBytes, -1, NULL, 0, NULL);
        return;
    }

    int first_pointer = offset/UFS_BLOCK_SIZE;
    unsigned char *p = (unsigned char *)dataBlockAddress(inode->direct[first_pointer]) + offset%UFS_BLOCK_SIZE;
    int next_pointer = first_pointer;

    int ctr = 0;
    unsigned char data[nbytes];
    while (ctr < nbytes)
    {
        data[ctr] = *p;
        p++;
        if (((unsigned long long)p - (unsigned long long)dataBlockAddress(inode->direct[next_pointer])) == UFS_BLOCK_SIZE)
        {
            p = (unsigned char *)dataBlockAddress(inode->direct[++next_pointer]);
        }
        ctr++;
    }
    
    packResponse(res, kSuccess, -1, NULL, nbytes, data);
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
        if (rc != sizeof(req))
        {
            // data received is incomplete
            packResponse(&res, kErrorChecksumFailed, -1, NULL, 0, NULL);
            goto sendresponse;
        }

        // Verify checksum
        word16 cksm = req.checksum;
        req.checksum = 0;
        word16 calc_cksm = UDP_Checksum((byte *)&req, sizeof(req));
        if (calc_cksm != cksm)
        {
            packResponse(&res, kErrorChecksumFailed, -1, NULL, 0, NULL);
            goto sendresponse;
        }

        // verify input checksum
        if (isInumValid(req.inum) == -1)
        {
            packResponse(&res, kErrorInvalidInum, -1, NULL, 0, NULL);
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
                readHandler(&req, &res);
                break;

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
    


 
