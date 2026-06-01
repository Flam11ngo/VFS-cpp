#pragma once

#include "Core.h"

class VirtualDisk;
class BlockManager;
class InodeCache;
class DirectoryManager;

/**
 * FileOperator — Layer 4: File operations and access control
 *
 * Manages the system-wide open-file table (file[40]) and provides
 * creat / open / close / delete / read / write / access.
 *
 * Operations that need user context receive a `user_t&` parameter
 * (owned by UserManager) rather than holding a reference to UserManager
 * itself — this avoids a circular dependency between Layer 4 and Layer 6.
 */
class FileOperator {
public:
    FileOperator(VirtualDisk& disk, BlockManager& blocks,
                 InodeCache& icache, DirectoryManager& dirs,
                 filsys& superblock);

    // -------- Access control --------

    /** Check whether `user` has `mode` access to `inode`.
     *  Returns 1 if permitted, 0 if denied. */
    uint32_t access(const user_t& user, inode* inode, uint16_t mode);

    // -------- File lifecycle --------

    /** Create a new file (or truncate existing). Returns user fd on success,
     *  -1 on error. */
    int32_t  creat(user_t& user, const char* filename, uint16_t mode);

    /** Open an existing file. Returns user fd on success,
     *  (uint16_t)-1 on error. */
    uint16_t open(user_t& user, const char* filename, uint16_t openmode);

    /** Close a file by user fd. */
    void     close(user_t& user, uint16_t cfd);

    /** Delete a file by name from the current directory. */
    void     delete_file(const char* filename);

    // -------- I/O --------

    /** Read up to `size` bytes from user fd into `buf`.
     *  Returns actual bytes read. */
    uint32_t read(user_t& user, uint32_t fd, char* buf, uint32_t size);

    /** Write up to `size` bytes from `buf` to user fd.
     *  Returns actual bytes written. */
    uint32_t write(user_t& user, uint32_t fd, const char* buf, uint32_t size);

    // -------- Table access --------

    file* ofile_table() { return m_ofile; }

private:
    VirtualDisk&      m_disk;
    BlockManager&     m_blocks;
    InodeCache&       m_icache;
    DirectoryManager& m_dirs;
    filsys&           m_superblock;
    file              m_ofile[SYSOPENFILE]{};
};
