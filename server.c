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
    unsigned int *temp = (unsigned int *)inodeBitmapAddr;
    unsigned int *t = temp + (int)(inum/32);
    unsigned int mask = (1<<(31-(inum%32)));
    if ((*t & mask) > 0)
        return 0;

    return -1;
}

int getFirstFreeDataBlock() {
    unsigned char *dataBitmapAddr = (unsigned char *)fs + fs->data_bitmap_addr*UFS_BLOCK_SIZE;
    unsigned int* temp = (unsigned int *)dataBitmapAddr;
    for (int j = 0; j < 1024; j++)  {
        if (j == 1) return -1;
        for (int i = 31; i >= 0; i--) {
            if (i == 0) return -1;
            unsigned int mask = (1<<i);
            if ((*temp & mask) == 0) {
                *temp = *temp | mask;
                return (fs->data_region_addr + (j*32) + (31-i));
            }
        }
        temp++;
    }

    return -1;
}

int getFirstFreeINode() {
    unsigned char *inodeBitmapAddr = (unsigned char *)fs + fs->inode_bitmap_addr*UFS_BLOCK_SIZE;
    unsigned int* temp = (unsigned int *)inodeBitmapAddr;
    for (int j = 0; j < 1024; j++) {
        for (int i = 31; i >= 0; i--) {
            unsigned int mask = (1<<i);
            if ((*temp & mask) == 0) {
                *temp = *temp | mask;
                return (j*32) + (31-i);
            } 
        }
        temp++;
    }

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
                if (isInumValid(entry->inum) == -1) {
                    packResponse(res, kErrorObjectDoesNotExist, -1, NULL, 0, NULL);
                    return;
                }
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

void removeFileOrDirectory(int inum) {
    unsigned char *inodeBitmapAddr = (unsigned char *)fs + fs->inode_bitmap_addr*UFS_BLOCK_SIZE;
    unsigned int *temp = (unsigned int *)inodeBitmapAddr;
    unsigned int *t = temp + (int)(inum/32);
    unsigned int mask = (1<<(31-(inum%32)));
    *t = *t & (~mask);
}

int isEmptyDir(inode_t *inode) {
    int numBlocks = inode->size/UFS_BLOCK_SIZE + 1;
    int c = 0;
    for (int i = 0; i < numBlocks; i++)
    {
        // check all data blocks of parent inode and all directory entries in each data block for 'name'
        dir_ent_t *data_blk = (dir_ent_t *)dataBlockAddress(inode->direct[i]);

        int numEntries = (i == numBlocks-1) ? (inode->size%UFS_BLOCK_SIZE)/sizeof(dir_ent_t) : UFS_BLOCK_SIZE/sizeof(dir_ent_t);
        for (int j = 0; j < numEntries; j++)
        {
            dir_ent_t *entry = data_blk + j;
            if (isInumValid(entry->inum) != -1) c++;
        }
    }

    if (c > 2) return 0;
    return 1;
}

void unlinkHandler(RPC_Request_t *req, RPC_Response_t *res) {
    int pinum = req->inum;
    char name[28];
    memcpy(name, req->name, 28);
    inode_t *parentINode = inodeEntryAddress(pinum);

    int numBlocks = parentINode->size/UFS_BLOCK_SIZE + 1;
    for (int i = 0; i < numBlocks; i++)
    {
        // check all data blocks of parent inode and all directory entries in each data block for 'name'
        dir_ent_t *data_blk = (dir_ent_t *)dataBlockAddress(parentINode->direct[i]);

        int numEntries = (i == numBlocks-1) ? (parentINode->size%UFS_BLOCK_SIZE)/sizeof(dir_ent_t) : UFS_BLOCK_SIZE/sizeof(dir_ent_t);
        for (int j = 0; j < numEntries; j++)
        {
            dir_ent_t *entry = data_blk + j;
            if (strcmp(entry->name, name) == 0)
            {
                inode_t *inode = inodeEntryAddress(entry->inum);
                printf("Inode size: %d\n", inode->size);
                if (inode->type == MFS_DIRECTORY && !isEmptyDir(inode)) {
                    packResponse(res, kErrorInvalidOffset, -1, NULL, 0, NULL);
                    return;
                }
                removeFileOrDirectory(entry->inum);
            }
        }
    }

    fsync(fsd);
    packResponse(res, kSuccess, -1, NULL, 0, NULL);
    return;

}

void createHandler(RPC_Request_t *req, RPC_Response_t *res) {
    int pinum = req->inum;
    int type = req->type;
    char name[28];
    memcpy(name, req->name, 28);
    inode_t *parentINode = inodeEntryAddress(pinum);
    if (parentINode->type != MFS_DIRECTORY) {
        packResponse(res, kErrorInvalidOffset, -1, NULL, 0, NULL);
        return;
    }

    int newInodeNum = getFirstFreeINode();
    inode_t *inode = inodeEntryAddress(newInodeNum);
    inode->type = type;
    inode->size = 0;


    // add entry to parent
    dir_ent_t dirEntry;
    dirEntry.inum = newInodeNum;
    memcpy(dirEntry.name, name, 28);
    int currentOffset = parentINode->size%UFS_BLOCK_SIZE;
    int last_block_pointer = parentINode->size/UFS_BLOCK_SIZE;
    if (currentOffset == 0) {
        int newBlockAddr = getFirstFreeDataBlock();
        parentINode->direct[last_block_pointer] = newBlockAddr;
    }
    unsigned char *p = (unsigned char *)dataBlockAddress(parentINode->direct[last_block_pointer]) + currentOffset;
    parentINode->size += 32;
    memcpy(p, &dirEntry, 32);

    printf("Created new node with inum: %d\n", newInodeNum);

    // add first 2 entries for directory
    if (type == MFS_DIRECTORY) {
        int newBlockAddr = getFirstFreeDataBlock();
        inode->direct[0] = newBlockAddr;
        inode->size = 64;
        p = (unsigned char *)dataBlockAddress(inode->direct[0]);
        dirEntry.inum = newInodeNum;
        strcpy(dirEntry.name, ".");
        memcpy(p, &dirEntry, 32);
        p = p + 32;
        dirEntry.inum = pinum;
        strcpy(dirEntry.name, "..");
        memcpy(p, &dirEntry, 32);
    }

 

    fsync(fsd);
    packResponse(res, kSuccess, -1, NULL, 0, NULL);
    return;
}

void writeHandler(RPC_Request_t *req, RPC_Response_t *res) {
    int inum = req->inum;
    int offset = req->offset;
    int nbytes = req->nbytes;

    char buffer[4096];
    memcpy(buffer, req->data, nbytes);


    inode_t *inode = inodeEntryAddress(inum);
    
    // check for errors
    if ( (inode->type == MFS_DIRECTORY))
    {
        packResponse(res, kErrorInvalidOffset, -1, NULL, 0, NULL);
        return;
    }


    int desiredSize = offset + nbytes;
    int last_block_pointer = inode->size/UFS_BLOCK_SIZE;
    if (desiredSize > inode->size) {
        // allocate new data blocks and change inode, databitmap data
        int diff = desiredSize - inode->size;
        int currentOffset = (inode->size)%UFS_BLOCK_SIZE;
        while (diff > 0) {
            if (currentOffset == 0) {
                // allocate block
                int newBlockAddress = getFirstFreeDataBlock();
                if (newBlockAddress == -1) {
                    packResponse(res, kErrorInvalidOffset, -1, NULL, 0, NULL);
                    return;
                }
                inode->direct[last_block_pointer] = newBlockAddress;
                diff -= UFS_BLOCK_SIZE;
                currentOffset = 0;
                last_block_pointer++;
            }
            else {
                // append to same block
                diff -= (UFS_BLOCK_SIZE - currentOffset);
                currentOffset = 0;
                last_block_pointer++;
            }
        }

        inode->size = desiredSize;
    }

    int first_pointer = offset/UFS_BLOCK_SIZE;
    unsigned char *p = (unsigned char *)dataBlockAddress(inode->direct[first_pointer]) + offset%UFS_BLOCK_SIZE;
    int next_pointer = first_pointer;

    // copy byte by byte
    int ctr = 0;
    while (ctr < nbytes)
    {
        *p = buffer[ctr++];
        p++;
        if (((unsigned long long)p - (unsigned long long)dataBlockAddress(inode->direct[next_pointer])) == UFS_BLOCK_SIZE)
        {
            p = (unsigned char *)dataBlockAddress(inode->direct[++next_pointer]);
        }
    }

    packResponse(res, kSuccess, -1, NULL, 0, NULL);
    fsync(fsd);
    return;
}

void readHandler(RPC_Request_t *req, RPC_Response_t *res)
{
    int inum = req->inum;
    int offset = req->offset;
    int nbytes = req->nbytes;

    inode_t *inode = inodeEntryAddress(inum);

    // check for errors
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

    // copy byte by byte
    int ctr = 0;
    unsigned char data[nbytes];
    while (ctr < nbytes)
    {
        data[ctr++] = *(p++);
        if (((unsigned long long)p - (unsigned long long)dataBlockAddress(inode->direct[next_pointer])) == UFS_BLOCK_SIZE)
        {
            p = (unsigned char *)dataBlockAddress(inode->direct[++next_pointer]);
        }
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
                writeHandler(&req, &res);
                break;

            case kRead:
                readHandler(&req, &res);
                break;

            case kCreat:
                createHandler(&req, &res);
                break;

            case kUnlink:
                unlinkHandler(&req, &res);
                break;

            case kShutdown:
                packResponse(&res, kSuccess, -1, NULL, 0, NULL);
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
    


 
