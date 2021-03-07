#pragma once

#include <stdint.h>
#include <stddef.h>

#include <List.h>
#include <Lock.h>

#include <Types.h>

#include <abi-bits/abi.h>

#define FD_SETSIZE 1024

#define PATH_MAX 4096
#define NAME_MAX 255

#define S_IFMT 0xF000
#define S_IFBLK 0x6000
#define S_IFCHR 0x2000
#define S_IFIFO 0x1000
#define S_IFREG 0x8000
#define S_IFDIR 0x4000
#define S_IFLNK 0xA000
#define S_IFSOCK 0xC000

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#define FS_NODE_TYPE 0xF000
#define FS_NODE_FILE S_IFREG
#define FS_NODE_DIRECTORY S_IFDIR//0x2
#define FS_NODE_MOUNTPOINT S_IFDIR//0x8
#define FS_NODE_BLKDEVICE S_IFBLK//0x10
#define FS_NODE_SYMLINK S_IFLNK//0x20
#define FS_NODE_CHARDEVICE S_IFCHR//0x40
#define FS_NODE_SOCKET S_IFSOCK//0x80

#define POLLIN 0x01
#define POLLOUT 0x02
#define POLLPRI 0x04
#define POLLHUP 0x08
#define POLLERR 0x10
#define POLLRDHUP 0x20
#define POLLNVAL 0x40
#define POLLWRNORM 0x80

#define O_ACCESS 7
#define O_EXEC 1
#define O_RDONLY 2
#define O_RDWR 3
#define O_SEARCH 4
#define O_WRONLY 5

#define O_APPEND __MLIBC_O_APPEND
#define O_CREAT __MLIBC_O_CREAT
#define O_DIRECTORY __MLIBC_O_DIRECTORY
#define O_EXCL __MLIBC_O_EXCL
#define O_NOCTTY __MLIBC_O_NOCTTY
#define O_NOFOLLOW __MLIBC_O_NOFOLLOW
#define O_TRUNC __MLIBC_O_TRUNC
#define O_NONBLOCK __MLIBC_O_NONBLOCK
#define O_DSYNC __MLIBC_O_DSYNC
#define O_RSYNC __MLIBC_O_RSYNC
#define O_SYNC __MLIBC_O_SYNC
#define O_CLOEXEC __MLIBC_O_CLOEXEC

#define POLLIN 0x01
#define POLLOUT 0x02
#define POLLPRI 0x04
#define POLLHUP 0x08
#define POLLERR 0x10
#define POLLRDHUP 0x20
#define POLLNVAL 0x40
#define POLLWRNORM 0x80

#define AT_EMPTY_PATH 1
#define AT_SYMLINK_FOLLOW 2
#define AT_SYMLINK_NOFOLLOW 4
#define AT_REMOVEDIR 8
#define AT_EACCESS 512

#define MAXIMUM_SYMLINK_AMOUNT 10

typedef int64_t ino_t;
typedef uint64_t dev_t;
typedef int32_t uid_t;
typedef int64_t off_t;
typedef int32_t mode_t;
typedef int32_t nlink_t;
typedef int64_t volume_id_t;

typedef struct {
	dev_t st_dev;
	ino_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	uid_t st_gid;
	dev_t st_rdev;
	off_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;
} stat_t;

class FsNode;

typedef struct fs_fd{
    FsNode* node;
    off_t pos;
    mode_t mode;
} fs_fd_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};
typedef struct {
	char fds_bits[128];
} fd_set_t;

static inline void FD_CLR(int fd, fd_set_t* fds) {
	assert(fd < FD_SETSIZE);
	fds->fds_bits[fd / 8] &= ~(1 << (fd % 8));
}

static inline int FD_ISSET(int fd, fd_set_t* fds) {
	assert(fd < FD_SETSIZE);
	return fds->fds_bits[fd / 8] & (1 << (fd % 8));
}

static inline void FD_SET(int fd, fd_set_t* fds) {
	assert(fd < FD_SETSIZE);
	fds->fds_bits[fd / 8] |= 1 << (fd % 8);
}

class FilesystemWatcher;
class DirectoryEntry;

class FsNode {
    friend class FilesystemBlocker;
public:
    lock_t blockedLock = 0;
    FastList<class FilesystemBlocker*> blocked;

    uint32_t flags = 0; // Flags
    uint32_t pmask = 0; // Permission mask
    uid_t uid = 0; // User id
    ino_t inode = 0; // Inode number
    size_t size = 0; // Node size
    int nlink = 0; // Amount of references/hard links
    unsigned handleCount = 0; // Amount of file handles that point to this node
    volume_id_t volumeID;

    int error = 0;

    virtual ~FsNode();

    /////////////////////////////
    /// \brief Read data from filesystem node
    ///
    /// Read data from filesystem node
    ///
    /// \param off Offset of data to read
    /// \param size Amount of data (in bytes) to read
    /// 
    /// \return Bytes read or if negative an error code
    /////////////////////////////
    virtual ssize_t Read(size_t off, size_t size, uint8_t* buffer); // Read Data
    
    /////////////////////////////
    /// \brief Write data to filesystem node
    ///
    /// Write data to filesystem node
    ///
    /// \param off Offset where data should be written
    /// \param size Amount of data (in bytes) to write
    /// 
    /// \return Bytes written or if negative an error code
    /////////////////////////////
    virtual ssize_t Write(size_t off, size_t size, uint8_t* buffer); // Write Data

    virtual fs_fd_t* Open(size_t flags); // Open
    virtual void Close(); // Close

    virtual int ReadDir(DirectoryEntry*, uint32_t); // Read Directory
    virtual FsNode* FindDir(char* name); // Find in directory

    virtual int Create(DirectoryEntry* ent, uint32_t mode);
    virtual int CreateDirectory(DirectoryEntry* ent, uint32_t mode);
    
    virtual ssize_t ReadLink(char* pathBuffer, size_t bufSize);
    virtual int Link(FsNode*, DirectoryEntry*);
    virtual int Unlink(DirectoryEntry*, bool unlinkDirectories = false);
    
    virtual int Truncate(off_t length);

    virtual int Ioctl(uint64_t cmd, uint64_t arg); // I/O Control
    virtual void Sync(); // Sync node to device

    virtual bool CanRead() { return true; }
    virtual bool CanWrite() { return true; }

    virtual void Watch(FilesystemWatcher& watcher, int events);
    virtual void Unwatch(FilesystemWatcher& watcher);

    virtual inline bool IsFile() { return (flags & FS_NODE_TYPE) == FS_NODE_FILE; }
    virtual inline bool IsDirectory() { return (flags & FS_NODE_TYPE) == FS_NODE_DIRECTORY; }
    virtual inline bool IsBlockDevice() { return (flags & FS_NODE_TYPE) == FS_NODE_BLKDEVICE; }
    virtual inline bool IsSymlink() { return (flags & FS_NODE_TYPE) == FS_NODE_SYMLINK; }
    virtual inline bool IsCharDevice() { return (flags & FS_NODE_TYPE) == FS_NODE_CHARDEVICE; }
    virtual inline bool IsSocket() { return (flags & FS_NODE_TYPE) == FS_NODE_SOCKET; }

    void UnblockAll();

    FsNode* link;
    FsNode* parent;

    FilesystemLock nodeLock; // Lock on FsNode info
};

class DirectoryEntry{
public:
    char name[NAME_MAX];

    FsNode* node = nullptr;
    uint32_t inode = 0;

    DirectoryEntry* parent = nullptr;

    mode_t flags = 0;

    DirectoryEntry(FsNode* node, const char* name);

    DirectoryEntry() {}
};

// FilesystemWatcher is a semaphore initialized to 0.
// A thread can wait on it like any semaphore,
// and when a file is ready it will signal and waiting thread(s) will get woken
class FilesystemWatcher : public Semaphore{
    List<fs_fd_t*> watching;
public:
    FilesystemWatcher() : Semaphore(0){

    }

    inline void WatchNode(FsNode* node, int events){
        fs_fd_t* desc = node->Open(0);
        assert(desc);

        desc->node->Watch(*this, events);

        watching.add_back(desc);
    }

    ~FilesystemWatcher(){
        for(auto& fd : watching){
            fd->node->Unwatch(*this);

            fd->node->Close();
        }
    }
};

class FilesystemBlocker : public ThreadBlocker {
    friend FsNode;
    friend FastList<FilesystemBlocker*>;
public:
    enum BlockType {
        BlockRead,
        BlockWrite,
    };
protected:
    FsNode* node = nullptr;

    FilesystemBlocker* next;
    FilesystemBlocker* prev;

    int blockType = BlockType::BlockRead;

    size_t requestedLength = 1; // How much data was requested?
public:
	FilesystemBlocker(FsNode* _node) : node(_node) {
        lock = 0;

        acquireLock(&node->blockedLock);
        node->blocked.add_back(this);
        releaseLock(&node->blockedLock);
    }

	FilesystemBlocker(FsNode* _node, size_t len) : node(_node), requestedLength(len) {
        lock = 0;

        acquireLock(&node->blockedLock);
        node->blocked.add_back(this);
        releaseLock(&node->blockedLock);
    }
    
    void Interrupt();

    inline void Unblock(){
        shouldBlock = false;

        acquireLock(&lock);
        if(node && !removed){ // It is assumed that the caller has acquired the node's blocked lock
            node->blocked.remove(this);

            removed = true;
        }

        if(thread){
            thread->Unblock();
        }
        releaseLock(&lock);
    }

    inline size_t RequestedLength() { return requestedLength; }

    ~FilesystemBlocker();
};

typedef struct fs_dirent {
	uint32_t inode; // Inode number
    uint32_t type;
	char name[NAME_MAX]; // Filename
} fs_dirent_t;

namespace fs{
    class FsVolume;

	extern List<FsVolume*>* volumes;

    void Initialize();
    FsNode* GetRoot();
	void RegisterVolume(FsVolume* vol);

    /////////////////////////////
    /// \brief Follow symbolic link
    ///
    /// \param link FsNode pointing to the link to be followed
    /// \param workingDir FsNode pointing to working directory for path resolution
    /// 
    /// \return FsNode of node in which the link points to on success, nullptr on failure
    /////////////////////////////
    FsNode* FollowLink(FsNode* link, FsNode* workingDir);

    /////////////////////////////
    /// \brief Resolve a path.
    ///
    /// \param path Path to resolve
    /// \param workingDir Path of working directory
    /// 
    /// \return FsNode which path points to, nullptr on failure
    /////////////////////////////
    FsNode* ResolvePath(const char* path, const char* workingDir = nullptr, bool followSymlinks = true);


    /////////////////////////////
    /// \brief Resolve a path.
    ///
    /// \param path Path to resolve
    /// \param workingDir Node of working directory
    /// 
    /// \return FsNode which path points to, nullptr on failure
    /////////////////////////////
	FsNode* ResolvePath(const char* path, FsNode* workingDir, bool followSymlinks = true);

    /////////////////////////////
    /// \brief Resolve parent directory of path.
    ///
    /// \param path Path of child for parent to resolve
    /// \param workingDir Path of working directory
    /// 
    /// \return FsNode of parent, nullptr on failure
    /////////////////////////////
    FsNode* ResolveParent(const char* path, const char* workingDir = nullptr);
    char* CanonicalizePath(const char* path, char* workingDir);
    char* BaseName(const char* path);

    /////////////////////////////
    /// \brief Read data from filesystem node
    ///
    /// Read data from filesystem node
    ///
    /// \param node Pointer to node to read from
    /// \param off Offset of data to read
    /// \param size Amount of data (in bytes) to read
    /// \param buffer Buffer to write into
    /// 
    /// \return Bytes read or if negative an error code
    /////////////////////////////
    ssize_t Read(FsNode* node, size_t offset, size_t size, void *buffer);
    
    /////////////////////////////
    /// \brief Write data to filesystem node
    ///
    /// Write data to filesystem node
    ///
    /// \param node Pointer to node to write to
    /// \param off Offset of write
    /// \param size Amount of data (in bytes) to write
    /// \param buffer Buffer to read from
    /// 
    /// \return Bytes written or if negative an error code
    /////////////////////////////
    ssize_t Write(FsNode* node, size_t offset, size_t size, void *buffer);
    fs_fd_t* Open(FsNode* node, uint32_t flags = 0);
    void Close(FsNode* node);
    void Close(fs_fd_t* handle);
    int ReadDir(FsNode* node, DirectoryEntry* dirent, uint32_t index);
    FsNode* FindDir(FsNode* node, char* name);
    
    ssize_t Read(fs_fd_t* handle, size_t size, uint8_t *buffer);
    ssize_t Write(fs_fd_t* handle, size_t size, uint8_t *buffer);
    int ReadDir(fs_fd_t* handle, DirectoryEntry* dirent, uint32_t index);
    FsNode* FindDir(fs_fd_t* handle, char* name);
    
    int Link(FsNode*, FsNode*, DirectoryEntry*);
    int Unlink(FsNode*, DirectoryEntry*, bool unlinkDirectories = false);

    int Ioctl(fs_fd_t* handle, uint64_t cmd, uint64_t arg);

    int Rename(FsNode* olddir, char* oldpath, FsNode* newdir, char* newpath);
}