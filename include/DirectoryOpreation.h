#pragma once

#include "Core.h"

class VirtualDisk;
class InodeCache;
class BlockManager;

/**
 * DirectoryManager — Layer 5: Directory operations
 *
 * Manages the current working directory (g_dir cache) and the
 * current-path inode pointer. Provides namei/iname lookups,
 * mkdir, chdir, and directory listing.
 */
class DirectoryManager {
public:
    DirectoryManager(VirtualDisk& disk, InodeCache& icache,
                     BlockManager& blocks);

    // -------- Directory-entry lookup --------

    /** Search current directory for `name`. Returns the entry index,
     *  or (uint32_t)-1 if not found. */
    uint32_t namei(const char* name);

    /** Find an empty directory-entry slot, copy `name` into it.
     *  Returns the slot index, or 0 on failure (directory full). */
    uint16_t iname(const char* name);

    // -------- Directory operations --------

    /** Create a subdirectory in the current directory. */
    void mkdir(const char* dirname, uint16_t uid_index);

    /** Change current directory to `dirname`. */
    void chdir(const char* dirname);

    /** List the contents of the current directory. */
    void dir_list();

    // -------- Current-directory accessors --------

    dir&    current_dir()      { return m_dir; }
    inode*  cur_path_inode()   { return m_cur_path_inode; }
    void    set_cur_path_inode(inode* ino) { m_cur_path_inode = ino; }

private:
    VirtualDisk&  m_disk;
    InodeCache&   m_icache;
    BlockManager& m_blocks;
    dir           m_dir{};
    inode*        m_cur_path_inode = nullptr;

    // -------- Helpers --------

    /** Write the cached g_dir back to the inode's data block on disk. */
    void writeback_dir(inode* ino);

    /** Load directory content from an inode's data block into m_dir. */
    void load_dir(inode* ino);
};
