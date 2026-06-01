#pragma once

#include "Core.h"

class VirtualDisk;
class InodeCache;

/**
 * BlockManager — Layer 2: Block and inode allocation / deallocation
 *
 * Manages the superblock (filsys) free-block and free-inode stacks.
 * Depends on VirtualDisk for disk I/O and InodeCache for iget() inside ialloc().
 */
class BlockManager {
public:
    BlockManager(VirtualDisk& disk, filsys& superblock);

    // -------- Block allocation --------

    /** Allocate a free disk block. Returns DISKFULL on failure. */
    uint32_t balloc();

    /** Return a disk block to the free pool. */
    void bfree(uint32_t block_num);

    // -------- Inode allocation --------

    /** Allocate a free disk inode, load it into memory via icache.iget(). */
    inode* ialloc(InodeCache& icache);

    /** Return a disk inode to the free pool. */
    void ifree(uint32_t dinodeid);

    // -------- Accessors --------

    filsys&      superblock() { return m_superblock; }
    VirtualDisk& disk()       { return m_disk; }

private:
    VirtualDisk& m_disk;
    filsys&      m_superblock;
};
