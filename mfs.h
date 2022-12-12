#ifndef __MFS_h__
#define __MFS_h__

#define MFS_DIRECTORY    (0)
#define MFS_REGULAR_FILE (1)
#define MFS_UNDEFINED (2)

#define MFS_BLOCK_SIZE   (4096)

typedef struct __MFS_Stat_t {
    int type;   // MFS_DIRECTORY or MFS_REGULAR
    int size;   // bytes
    // note: no permissions, access times, etc.
} MFS_Stat_t;

typedef struct __MFS_DirEnt_t {
    char name[28];  // up to 28 bytes of name in directory (including \0)
    int  inum;      // inode number of entry (-1 means entry not used)
} MFS_DirEnt_t;

enum Operation { kInit, kLookup, kStat, kWrite, kRead, kCreat, kUnlink, kShutdown };
enum ErrorCode {    
                kSuccess, 
                kError, 
                kErrorInvalidInum, 
                kErrorObjectDoesNotExist,
                kErrorInvalidNBytes,
                kErrorInvalidOffset,
                kErrorNotAFile,
                kErrorObjectNameTooLong,
                kErrorDirectoryNotEmpty,
                kErrorChecksumFailed
};

typedef struct __RPC_Request_t {
    
    // operation
    enum Operation op;
    
    // parameters
    int inum;
    int offset;
    int nbytes;
    int type;
    char name[28];
    
    // reliability
    unsigned short checksum;

	// data
	unsigned char data[4096];
} RPC_Request_t;

typedef struct __RPC_Response_t {
    enum ErrorCode errorCode;

    int inum;           // for MFS_Init, MFS_Lookup
    MFS_Stat_t stat;    // for MFS_Stat
    int nbytes;         // for MFS_Read

    // reliability
    unsigned short checksum;

	// data
	unsigned char data[4096];

} RPC_Response_t;

// operations (listed 0, 1, ...7)
int MFS_Init(char *hostname, int port);     // speak with the server to get root inum
int MFS_Lookup(int pinum, char *name);
int MFS_Stat(int inum, MFS_Stat_t *m);
int MFS_Write(int inum, char *buffer, int offset, int nbytes);
int MFS_Read(int inum, char *buffer, int offset, int nbytes);
int MFS_Creat(int pinum, int type, char *name);
int MFS_Unlink(int pinum, char *name);
int MFS_Shutdown();

#endif // __MFS_h__
