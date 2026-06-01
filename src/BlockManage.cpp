/**
 * BlockManage.cpp — Layer 2: Block and inode allocation / deallocation
 */
#include <cstdlib>
#include <cstring>

#include "Core.h"

// ====== Constructor ======

BlockManager::BlockManager(VirtualDisk& disk, filsys& superblock)
    : m_disk(disk), m_superblock(superblock)
{
}

// ====== balloc() ======

unsigned int BlockManager::balloc()
{
    if (m_superblock.s_nfree == 0) {
        printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_NOSPC));
        return DISKFULL;
    }

    unsigned int free_block = m_superblock.s_free[m_superblock.s_pfree];

    if (m_superblock.s_nfree == 1) {
        char block[BLOCKSIZ];
        memset(block, 0, BLOCKSIZ);

        fseek(m_disk.handle(), DATASTART + free_block * BLOCKSIZ, SEEK_SET);
        fread(block, BLOCKSIZ, 1, m_disk.handle());
        unsigned int next_nfree = ((unsigned int*)block)[0];
        if (next_nfree > 0 && next_nfree <= NICFREE) {
            memcpy(m_superblock.s_free, block + sizeof(unsigned int),
                   next_nfree * sizeof(unsigned int));
            m_superblock.s_nfree = next_nfree;
            m_superblock.s_pfree = next_nfree - 1;
        } else {
            m_superblock.s_nfree = 0;
            m_superblock.s_pfree = 0;
        }
    } else {
        m_superblock.s_pfree--;
        m_superblock.s_nfree--;
    }

    m_superblock.s_fmod = SUPDATE;
    return free_block;
}

// ====== bfree() ======

void BlockManager::bfree(unsigned int block_num)
{
    if (m_superblock.s_nfree == NICFREE) {
        char block[BLOCKSIZ];
        memset(block, 0, BLOCKSIZ);
        unsigned int* block_ptr = (unsigned int*)block;
        block_ptr[0] = m_superblock.s_nfree;
        memcpy(block + sizeof(unsigned int), m_superblock.s_free,
               NICFREE * sizeof(unsigned int));
        fseek(m_disk.handle(), DATASTART + block_num * BLOCKSIZ, SEEK_SET);
        fwrite(block, BLOCKSIZ, 1, m_disk.handle());

        m_superblock.s_free[0] = block_num;
        m_superblock.s_nfree = 1;
        m_superblock.s_pfree = 0;
    } else {
        m_superblock.s_pfree++;
        m_superblock.s_nfree++;
        m_superblock.s_free[m_superblock.s_pfree] = block_num;
    }

    m_superblock.s_fmod = SUPDATE;
}

// ====== ialloc() ======

inode* BlockManager::ialloc(InodeCache& icache)
{
    if (m_superblock.s_ninode == 0) {
        unsigned int count = 0;
        dinode disk_inode;
        for (unsigned int i = m_superblock.s_rinode;
             i < DINODEBLK * (BLOCKSIZ / DINODESIZ); i++) {
            fseek(m_disk.handle(), DINODESTART + i * DINODESIZ, SEEK_SET);
            fread(&disk_inode, sizeof(dinode), 1, m_disk.handle());
            if (disk_inode.di_number == 0) {
                m_superblock.s_inode[count++] = i;
                if (count >= NICINOD) {
                    m_superblock.s_rinode = i + 1;
                    break;
                }
            }
        }
        if (count == 0) {
            printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_NOSPC));
            return nullptr;
        }
        m_superblock.s_ninode = count;
        m_superblock.s_pinode = count - 1;
    }

    unsigned int dinode_id = m_superblock.s_inode[m_superblock.s_pinode];
    m_superblock.s_pinode--;
    m_superblock.s_ninode--;
    m_superblock.s_fmod = SUPDATE;

    inode* ino = icache.iget(dinode_id);
    if (!ino) {
        printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_IO));
        return nullptr;
    }

    dinode empty_inode;
    memset(&empty_inode, 0, sizeof(empty_inode));
    empty_inode.di_number = 1;

    fseek(m_disk.handle(), DINODESTART + dinode_id * DINODESIZ, SEEK_SET);
    fwrite(&empty_inode, sizeof(empty_inode), 1, m_disk.handle());

    /* Sync memory inode: iget() loaded stale (zeroed) data before this write */
    ino->di_number = 1;

    return ino;
}

// ====== ifree() ======

void BlockManager::ifree(unsigned int dinodeid)
{
    if (m_superblock.s_ninode < NICINOD) {
        m_superblock.s_ninode++;
        m_superblock.s_pinode++;
        m_superblock.s_inode[m_superblock.s_pinode] = dinodeid;
    } else {
        printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_NOSPC));
    }
    if (dinodeid < m_superblock.s_rinode) {
        m_superblock.s_rinode = dinodeid;
    }

    m_superblock.s_fmod = SUPDATE;
}
