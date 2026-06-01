#pragma once

#include <cstdint>
#include <cstdio>
#include <array>
#include <vector>

/* ========================================================================
 * Core.h — Shared types, constants, error codes, and the VFS facade
 * ======================================================================== */

// ====== Disk Layout & System Constants ======
inline constexpr uint16_t BLOCKSIZ     = 512;
inline constexpr uint16_t SYSOPENFILE  = 40;
inline constexpr uint16_t DIRNUM       = 128;
inline constexpr uint16_t DIRSIZ       = 14;
inline constexpr uint16_t PWDSIZ       = 12;
inline constexpr uint16_t PWDNUM       = 32;
inline constexpr uint16_t NOFILE       = 20;
inline constexpr uint16_t NADDR        = 10;
inline constexpr uint16_t NHINO        = 128;
inline constexpr uint16_t USERNUM      = 10;
inline constexpr uint16_t DINODESIZ    = 32;
inline constexpr uint16_t DINODEBLK    = 32;
inline constexpr uint16_t FILEBLK      = 512;
inline constexpr uint16_t NICFREE      = 50;
inline constexpr uint16_t NICINOD      = 50;

inline constexpr uint32_t DINODESTART  = 2 * BLOCKSIZ;
inline constexpr uint32_t DATASTART    = (2 + DINODEBLK) * BLOCKSIZ;

// ====== Inode Type & Permission Flags ======
inline constexpr uint16_t DIEMPTY    = 0x0000;
inline constexpr uint16_t DIFILE     = 0x1000;
inline constexpr uint16_t DIDIR      = 0x2000;

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

inline constexpr uint16_t IUPDATE = 0x0002;
inline constexpr uint16_t SUPDATE = 0x0001;

inline constexpr uint16_t FREAD   = 0x0001;
inline constexpr uint16_t FWRITE  = 0x0002;
inline constexpr uint16_t FAPPEND = 0x0004;

inline constexpr uint32_t DISKFULL = 65535;

// ====== Error Codes ======
enum class VfsError {
    E_VFS_OK = 0,
    E_VFS_NOENT,
    E_VFS_NOSPC,
    E_VFS_NOPERM,
    E_VFS_EXIST,
    E_VFS_NOTDIR,
    E_VFS_ISDIR,
    E_VFS_NFILE,
    E_VFS_IO,
    E_VFS_AUTH,
    E_VFS_BUSY,
    E_VFS_INVAL,
};

const char* vfs_strerror(VfsError err);
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

// ====== Disk Inode  (32 bytes — MUST stay POD for fread/fwrite) ======
struct dinode {
    uint16_t di_number;
    uint16_t di_mode;
    uint16_t di_uid;
    uint16_t di_gid;
    uint32_t di_size;
    uint16_t di_addr[NADDR];       // raw array — same layout as std::array
};

// ====== Directory Entry  (16 bytes — MUST stay POD for disk blocks) ======
struct direct {
    char     d_name[DIRSIZ];
    uint32_t d_ino;
};

// ====== Superblock  (MUST stay POD — read/written directly to disk) ======
struct filsys {
    uint16_t s_isize;
    uint32_t s_fsize;
    uint32_t s_nfree;
    uint16_t s_pfree;
    uint32_t s_free[NICFREE];       // raw array — same layout as std::array
    uint32_t s_ninode;
    uint16_t s_pinode;
    uint32_t s_inode[NICINOD];
    uint32_t s_rinode;
    char     s_fmod;
};

// ====== Password Entry  (MUST stay POD — read/written to disk) ======
struct pwd {
    uint16_t p_uid;
    uint16_t p_gid;
    char     password[PWDSIZ];
};

// ====== Current Directory  (memory only — can use STL) ======
struct dir {
    std::vector<direct> entries;
};

// ====== Inode Hash Table Header ======
struct hinode {
    inode *i_forw;
};

// ====== System Open File Table Entry ======
struct file {
    char     f_flag   = 0;
    uint32_t f_count  = 0;
    inode   *f_inode  = nullptr;
    uint32_t f_off    = 0;
};

// ====== User Structure ======
struct user_t {
    uint16_t                    u_default_mode = DEFAULTMODE;
    uint16_t                    u_uid          = 0;
    uint16_t                    u_gid          = 0;
    std::array<uint16_t, NOFILE> u_ofile{};
};

// ====== Memory Inode ======
struct inode {
    inode    *i_forw = nullptr;
    inode    *i_back = nullptr;
    char      i_flag = 0;
    uint32_t  i_ino  = 0;
    uint32_t  i_count = 0;
    uint16_t  di_number = 0;
    uint16_t  di_mode   = 0;
    uint16_t  di_uid    = 0;
    uint16_t  di_gid    = 0;
    uint16_t  di_size   = 0;
    std::array<uint32_t, NADDR> di_addr{};
};

// ====== VFS System Facade ======

class Core {
public:
    Core();
    ~Core();

    void init();
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

extern Core g_core;

// ====== Subsystem headers ======
#include "VirtualDisk.h"
#include "BlockManage.h"
#include "InodeCache.h"
#include "DirectoryOpreation.h"
#include "FileOperation.h"
#include "User.h"
#include "Shell.h"
