#pragma once

#include "Core.h"
#include <unordered_map>

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

    /** Load directory content from an inode's data block into m_dir. */
    void load_dir(inode* ino);

    /** Clear the in-memory directory cache (called after format). */
    void clear_cache() { m_dir_cache.clear(); }

    /** Sync current m_dir into the cache (call after direct entry edits). */
    void sync_cache(inode* ino) {
        if (ino) m_dir_cache[ino->i_ino] = m_dir.entries;
    }

private:
    VirtualDisk&  m_disk;
    InodeCache&   m_icache;
    BlockManager& m_blocks;
    dir           m_dir{};
    inode*        m_cur_path_inode = nullptr;

    // -------- Helpers --------

    void writeback_dir(inode* ino);

    /** In-memory cache of directory contents keyed by inode number.
     *  writeback_dir stores here; load_dir fetches from here first. */
    std::unordered_map<uint32_t, std::vector<direct>> m_dir_cache;
};
