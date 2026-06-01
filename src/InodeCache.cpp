/**
 * InodeCache.cpp — Layer 3: Memory-inode hash-table cache
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Core.h"

// ====== Constructor ======

InodeCache::InodeCache(VirtualDisk& disk, BlockManager& blocks)
    : m_disk(disk), m_blocks(blocks)
{
    for (int i = 0; i < NHINO; i++) {
        m_hash[i].i_forw = nullptr;
    }
}

// ====== Destructor ======

InodeCache::~InodeCache()
{
    for (int i = 0; i < NHINO; i++) {
        inode* p = m_hash[i].i_forw;
        while (p) {
            inode* next = p->i_forw;
            free(p);
            p = next;
        }
    }
}

// ====== iget() ======

inode *InodeCache::iget(unsigned int dinodeid)
{
    unsigned int hash = dinodeid % NHINO;

    inode *p = m_hash[hash].i_forw;
    while (p) {
        if (p->i_ino == dinodeid) {
            p->i_count++;
            return p;
        }
        p = p->i_forw;
    }

    inode *new_inode = (inode *)malloc(sizeof(inode));
    if (!new_inode) {
        printf("Error: malloc inode failed.\n");
        return nullptr;
    }
    memset(new_inode, 0, sizeof(inode));

    dinode disk_inode;
    fseek(m_disk.handle(), DINODESTART + dinodeid * DINODESIZ, SEEK_SET);
    fread(&disk_inode, sizeof(dinode), 1, m_disk.handle());

    new_inode->di_number = disk_inode.di_number;
    new_inode->di_mode   = disk_inode.di_mode;
    new_inode->di_uid    = disk_inode.di_uid;
    new_inode->di_gid    = disk_inode.di_gid;
    new_inode->di_size   = disk_inode.di_size;
    std::copy(std::begin(disk_inode.di_addr), std::end(disk_inode.di_addr),
              new_inode->di_addr.begin());

    new_inode->i_ino   = dinodeid;
    new_inode->i_count = 1;
    new_inode->i_flag  = 0;

    new_inode->i_forw = m_hash[hash].i_forw;
    new_inode->i_back = (inode *)&m_hash[hash];
    if (m_hash[hash].i_forw) {
        m_hash[hash].i_forw->i_back = new_inode;
    }
    m_hash[hash].i_forw = new_inode;

    return new_inode;
}

// ====== iput() ======

void InodeCache::iput(inode *pinode)
{
    if (!pinode) return;

    pinode->i_count--;
    if (pinode->i_count > 0) return;

    if (pinode->di_number == 0) {
        for (int i = 0; i < NADDR; i++) {
            if (pinode->di_addr[i] != 0) {
                m_blocks.bfree(pinode->di_addr[i]);
                pinode->di_addr[i] = 0;
            }
        }
        m_blocks.ifree(pinode->i_ino);

        dinode empty;
        memset(&empty, 0, sizeof(empty));
        fseek(m_disk.handle(), DINODESTART + pinode->i_ino * DINODESIZ, SEEK_SET);
        fwrite(&empty, sizeof(empty), 1, m_disk.handle());
        fflush(m_disk.handle());
    } else {
        dinode disk_inode;
        memset(&disk_inode, 0, sizeof(disk_inode));
        disk_inode.di_number = pinode->di_number;
        disk_inode.di_mode   = pinode->di_mode;
        disk_inode.di_uid    = pinode->di_uid;
        disk_inode.di_gid    = pinode->di_gid;
        disk_inode.di_size   = pinode->di_size;
        std::copy(pinode->di_addr.begin(), pinode->di_addr.end(),
                  disk_inode.di_addr);

        fseek(m_disk.handle(), DINODESTART + pinode->i_ino * DINODESIZ, SEEK_SET);
        fwrite(&disk_inode, sizeof(disk_inode), 1, m_disk.handle());
        fflush(m_disk.handle());
    }

    if (pinode->i_back) {
        pinode->i_back->i_forw = pinode->i_forw;
    }
    if (pinode->i_forw) {
        pinode->i_forw->i_back = pinode->i_back;
    }

    free(pinode);
}
