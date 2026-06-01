#pragma once

#include <cstdint>
#include <cstdio>

/* ========================================================================
 * Core.h — Shared types, constants, and error codes for the VFS system
 * ======================================================================== */

// ====== Disk Layout & System Constants ======
inline constexpr uint16_t BLOCKSIZ     = 512;   /* block size in bytes */
inline constexpr uint16_t SYSOPENFILE  = 40;    /* max system open files */
inline constexpr uint16_t DIRNUM       = 128;   /* max directory entries per dir */
inline constexpr uint16_t DIRSIZ       = 14;    /* directory entry name size */
inline constexpr uint16_t PWDSIZ       = 12;    /* password string size */
inline constexpr uint16_t PWDNUM       = 32;    /* max password entries */
inline constexpr uint16_t NOFILE       = 20;    /* max open files per user */
inline constexpr uint16_t NADDR        = 10;    /* direct index blocks per inode */
inline constexpr uint16_t NHINO        = 128;   /* hash chain count (power of 2) */
inline constexpr uint16_t USERNUM      = 10;    /* max concurrent users */
inline constexpr uint16_t DINODESIZ    = 32;    /* disk inode size in bytes */
inline constexpr uint16_t DINODEBLK    = 32;    /* inode area block count */
inline constexpr uint16_t FILEBLK      = 512;   /* data area block count */
inline constexpr uint16_t NICFREE      = 50;    /* superblock free block stack size */
inline constexpr uint16_t NICINOD      = 50;    /* superblock free inode stack size */

inline constexpr uint32_t DINODESTART  = 2 * BLOCKSIZ;
inline constexpr uint32_t DATASTART    = (2 + DINODEBLK) * BLOCKSIZ;

// ====== Inode Type & Permission Flags ======
inline constexpr uint16_t DIEMPTY    = 0x0000;  /* empty inode */
inline constexpr uint16_t DIFILE     = 0x1000;  /* regular file */
inline constexpr uint16_t DIDIR      = 0x2000;  /* directory */

inline constexpr uint16_t UDIREAD    = 0x0001;
inline constexpr uint16_t UDIWRITE   = 0x0002;
inline constexpr uint16_t UDIEXECUTE = 0x0004;
inline constexpr uint16_t GDIREAD    = 0x0010;
inline constexpr uint16_t GDIWRITE   = 0x0020;
inline constexpr uint16_t GDIEXECUTE = 0x0040;
inline constexpr uint16_t ODIREAD    = 0x0100;
inline constexpr uint16_t ODIWRITE   = 0x0200;
inline constexpr uint16_t ODIEXECUTE = 0x0400;
inline constexpr uint16_t DEFAULTMODE = 0x0777;

// ====== File & Superblock Flags ======
inline constexpr unsigned short READ_MODE    = 1;
inline constexpr unsigned short WRITE_MODE   = 2;
inline constexpr unsigned short EXECUTE_MODE = 3;

inline constexpr uint16_t IUPDATE = 0x0002;  /* inode modified */
inline constexpr uint16_t SUPDATE = 0x0001;  /* superblock modified */

inline constexpr uint16_t FREAD   = 0x0001;
inline constexpr uint16_t FWRITE  = 0x0002;
inline constexpr uint16_t FAPPEND = 0x0004;

inline constexpr uint32_t DISKFULL = 65535;  /* disk full sentinel */

// ====== Error Codes ======
enum class VfsError {
    E_VFS_OK = 0,      /* success */
    E_VFS_NOENT,       /* no such file or directory */
    E_VFS_NOSPC,       /* no space left on device */
    E_VFS_NOPERM,      /* permission denied */
    E_VFS_EXIST,       /* file already exists */
    E_VFS_NOTDIR,      /* not a directory */
    E_VFS_ISDIR,       /* is a directory */
    E_VFS_NFILE,       /* too many open files / table full */
    E_VFS_IO,          /* I/O error */
    E_VFS_AUTH,        /* authentication failure */
    E_VFS_BUSY,        /* resource busy */
    E_VFS_INVAL,       /* invalid argument */
};

const char* vfs_strerror(VfsError err);
// Overload for backward compatibility with integer error codes
inline const char* vfs_strerror(int err) {
    return vfs_strerror(static_cast<VfsError>(err));
}

// ====== Forward Declarations ======
struct inode;
class VirtualDisk;
class BlockManager;
class InodeCache;
class DirectoryManager;
class FileOperator;
class UserManager;

// ====== Disk Inode (32 bytes) ======
struct dinode {
    uint16_t di_number;       /* link count */
    uint16_t di_mode;         /* type + permissions */
    uint16_t di_uid;          /* owner uid */
    uint16_t di_gid;          /* owner gid */
    uint32_t di_size;         /* file size in bytes */
    uint16_t di_addr[NADDR];  /* direct block pointers */
};

// ====== Directory Entry (16 bytes) ======
struct direct {
    char     d_name[DIRSIZ];  /* file name */
    uint32_t d_ino;           /* inode number */
};

// ====== Superblock ======
struct filsys {
    uint16_t s_isize;            /* inode area block count */
    uint32_t s_fsize;            /* data area block count */
    uint32_t s_nfree;            /* free block count in stack */
    uint16_t s_pfree;            /* free block stack pointer */
    uint32_t s_free[NICFREE];    /* free block stack */
    uint32_t s_ninode;           /* free inode count in stack */
    uint16_t s_pinode;           /* free inode stack pointer */
    uint32_t s_inode[NICINOD];   /* free inode stack */
    uint32_t s_rinode;           /* remembered inode */
    char     s_fmod;             /* modified flag */
};

// ====== Password Entry ======
struct pwd {
    uint16_t p_uid;
    uint16_t p_gid;
    char     password[PWDSIZ];
};

class Core {
public:
    Core();
    ~Core();

    /** Format (if needed), install, load root dir, login. */
    void init();

    /** Graceful shutdown: writeback, logout, close disk. */
    void exit();

    VirtualDisk&      disk()       { return *m_disk; }
    BlockManager&     blocks()     { return *m_blocks; }
    InodeCache&       icache()     { return *m_icache; }
    DirectoryManager& dirs()       { return *m_dirs; }
    FileOperator&     files()      { return *m_files; }
    UserManager&      users()      { return *m_users; }
    filsys&           superblock() { return m_superblock; }

private:
    filsys            m_superblock{};
    VirtualDisk*      m_disk   = nullptr;
    BlockManager*     m_blocks = nullptr;
    InodeCache*       m_icache = nullptr;
    DirectoryManager* m_dirs   = nullptr;
    FileOperator*     m_files  = nullptr;
    UserManager*      m_users  = nullptr;
};

/** Single global VFS instance. */
extern Core g_core;

// ====== Remaining structs (needed by subsystem headers) ======

struct dir {
    direct   direct[DIRNUM];
    int32_t size;
};

struct hinode {
    inode *i_forw;
};

struct file {
    char     f_flag;
    uint32_t f_count;
    inode   *f_inode;
    uint32_t f_off;
};

struct user_t {
    uint16_t u_default_mode;
    uint16_t u_uid;
    uint16_t u_gid;
    uint16_t u_ofile[NOFILE];
};

struct inode {
    inode    *i_forw;
    inode    *i_back;
    char      i_flag;
    uint32_t  i_ino;
    uint32_t  i_count;
    uint16_t  di_number;
    uint16_t  di_mode;
    uint16_t  di_uid;
    uint16_t  di_gid;
    uint16_t  di_size;
    uint32_t  di_addr[NADDR];
};

// ====== Subsystem headers (must come last: they need all types above) ======
#include "VirtualDisk.h"
#include "BlockManage.h"
#include "InodeCache.h"
#include "DirectoryOpreation.h"
#include "FileOperation.h"
#include "User.h"
#include "Shell.h"
