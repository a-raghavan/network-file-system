#include <poll.h>
#include "mfs.h"
#include "udp.h"

// Copy-On-Write globals
int sd;
struct sockaddr_in addrSnd;

void packRequest(RPC_Request_t *req, enum Operation op, int inum, int offset, int nbytes, int type, char *name, unsigned char *data)
{
    req->op = op;
    req->inum = inum;
    req->offset = offset;
    req->nbytes = nbytes;
    req->type = type;
    req->checksum = 0;
    if (name)
        memcpy(req->name, name, 28);
    memcpy(req->data, data, nbytes);
}

void sendRetry(RPC_Request_t *req, RPC_Response_t *res)
{
    struct sockaddr_in addrRcv;
    int ret;

    // poll retry loop
    while (1)
    {
        //printf("name %s, checksum %d operation %d \n", req->name, req->checksum, req->op);
        ret = UDP_Write(sd, &addrSnd, (char *)req, sizeof(RPC_Request_t));
        
        printf("client :: sent request\n");

        if (ret < 0) {
            printf("client:: failed to send\n");
            exit(1);
        }
        
        struct pollfd fds[2];

        fds[0].fd = sd;
        fds[0].events = POLLIN;

        ret = poll(fds, 2, 2*1000);
        if (ret == -1)
        {
            printf("client:: poll failed\n");
            exit(1);
        }

        if (!ret)
        {
            printf("client:: timeout retry\n");
            continue;
        }

        ret = UDP_Read(sd, &addrRcv, (char *)res, sizeof(RPC_Response_t));

        printf("client :: code %d\n", res->errorCode);

        // retry operation if message to server is corrupted or out of order.
        if (res->errorCode == kErrorChecksumFailed)
            continue;

        return;
    }
}


int MFS_Init(char *hostname, int port)
{
    // client socket
    sd = UDP_Open(20000);

    // bind server socket address
    if (UDP_FillSockAddr(&addrSnd, hostname, port) == -1)
    {
        printf("can't find address\n");
        return -1;
    }

    return 0;
}

int MFS_Lookup(int pinum, char *name)
{
    // name length too long
    if (strlen(name) > 27)
        return -1;
    
    RPC_Request_t req;
    packRequest(&req, kLookup, pinum, 0, 0, MFS_UNDEFINED, name, NULL);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);

    if (res.errorCode == kSuccess)
        return res.inum;

    return -1;
}

int MFS_Stat(int inum, MFS_Stat_t *m)
{
    RPC_Request_t req;
    packRequest(&req, kStat, inum, 0, 0, MFS_UNDEFINED, NULL, NULL);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);
    if (res.errorCode != kSuccess)
        return -1;
        
    m->size = res.stat.size;
    m->type = res.stat.type;

    return 0;
}

int MFS_Write(int inum, char *buffer, int offset, int nbytes)
{
    RPC_Request_t req;
    packRequest(&req, kWrite, inum, offset, nbytes, MFS_UNDEFINED, NULL, (unsigned char *)buffer);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);

    if (res.errorCode != kSuccess)
        return -1;
    return 0;
}

int MFS_Read(int inum, char *buffer, int offset, int nbytes)
{
    RPC_Request_t req;
    packRequest(&req, kRead, inum, offset, nbytes, MFS_UNDEFINED, NULL, NULL);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);
    
    if (res.errorCode != kSuccess)
        return -1;

    memcpy(buffer, res.data, nbytes);
    return 0;
    
}
int MFS_Creat(int pinum, int type, char *name)
{
    RPC_Request_t req;
    packRequest(&req, kCreat, pinum, 0, 0, type, name, NULL);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);

    if (res.errorCode != kSuccess)
        return -1;
    return 0;
}

int MFS_Unlink(int pinum, char *name)
{
    RPC_Request_t req;
    packRequest(&req, kUnlink, pinum, 0, 0, MFS_UNDEFINED, name, NULL);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);

    if (res.errorCode != kSuccess)
        return -1;
    return 0;
}

int MFS_Shutdown()
{
    RPC_Request_t req;
    packRequest(&req, kShutdown, -1, 0, 0, MFS_UNDEFINED, NULL, NULL);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);

    return 0;
}

