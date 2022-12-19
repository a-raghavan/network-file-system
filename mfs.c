#include <poll.h>
#include <time.h>
#include <stdlib.h>
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
    if (data)
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
    if (!hostname || port < 0 || port > 65535)
        return -1;

    // client socket
    int MIN_PORT = 20000;
    int MAX_PORT = 40000;

    srand(time(0));
    int port_num = (rand() % (MAX_PORT - MIN_PORT) + MIN_PORT);
    sd = UDP_Open(port_num);

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
    if (!name || strlen(name) > 27 || pinum < 0)
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
    if (inum < 0 || !m)
        return -1;

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
    if (inum < 0 || offset < 0 || nbytes < 0 || nbytes > 4096)
        return -1;
    
    if (nbytes == 0)
        return 0;

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
    if (inum < 0 || !buffer || offset < 0 || nbytes < 0 || nbytes > 4096)
        return -1;

    if (nbytes == 0)
        return 0;
    
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
    if (pinum < 0 || type > 1 || type < 0 || !name || strlen(name) > 27)
        return -1;
    
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
    if (pinum < 0 || !name || strlen(name) > 27)
        return -1;
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
    packRequest(&req, kShutdown, 0, 0, 0, MFS_UNDEFINED, NULL, NULL);
    req.checksum = UDP_Checksum((byte *)&req, sizeof(req));
    RPC_Response_t res;
    sendRetry(&req, &res);

    return 0;
}

