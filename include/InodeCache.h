#pragma once

#include "Core.h"

class VirtualDisk;
class BlockManager;

/**
 * InodeCache — Layer 3: Memory-inode hash-table cache
 *
 * Owns the 128-bucket hinode hash table and all malloc'd inode nodes.
 * Provides iget() (hash lookup + disk load on miss) and iput()
 * (refcount decrement, writeback, and potential reclamation).
 */
class InodeCache {
public:
    InodeCache(VirtualDisk& disk, BlockManager& blocks);

    // Non-copyable (manages linked-list pointers that must stay fixed)
    InodeCache(const InodeCache&) = delete;
    InodeCache& operator=(const InodeCache&) = delete;

    ~InodeCache();

    // -------- Inode lifecycle --------

    /** Get the memory inode for `dinodeid` (loads from disk on cache miss).
     *  Increments the reference count. Returns nullptr on error. */
    inode* iget(uint32_t dinodeid);

    /** Release a reference to a memory inode.
     *  When i_count reaches 0 the inode is written back to disk.
     *  If di_number is also 0 the data blocks and disk inode are freed. */
    void iput(inode* pinode);

    // -------- Accessors --------

    hinode* hash_table() { return m_hash; }

    /** Clear all cached inodes (called after format). */
    void clear();

private:
    VirtualDisk&  m_disk;
    BlockManager& m_blocks;
    hinode        m_hash[NHINO];   // 128 hash-chain heads
};
