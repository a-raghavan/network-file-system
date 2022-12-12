#ifndef __UDP_h__
#define __UDP_h__

//
// includes
// 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <netinet/tcp.h>
#include <netinet/in.h>

//
// prototypes
// 

typedef unsigned char      byte;    // Byte is a char
typedef unsigned short int word16;  // 16-bit word is a short int
typedef unsigned int       word32;  // 32-bit word is an int

word16 UDP_Checksum(byte *addr, word32 count);

int UDP_Open(int port);
int UDP_Close(int fd);

int UDP_Read(int fd, struct sockaddr_in *addr, char *buffer, int n);
int UDP_Write(int fd, struct sockaddr_in *addr, char *buffer, int n);

int UDP_FillSockAddr(struct sockaddr_in *addr, char *hostName, int port);

#endif // __UDP_h__

