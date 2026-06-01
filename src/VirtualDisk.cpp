/**
 * VirtualDisk.cpp — Layer 1: Virtual disk format / install / I/O
 */
#include <cstdlib>
#include <cstring>

#include "Core.h"

// ====== Destructor ======

VirtualDisk::~VirtualDisk()
{
    if (m_fd) {
        fclose(m_fd);
        m_fd = nullptr;
    }
}

// ====== format() ======

void VirtualDisk::format(filsys& superblock)
{
    printf("Formatting\n");
    m_fd = fopen("filesystem", "w+b");
    if (!m_fd) {
        printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_IO));
        exit(1);
    }
    char block[BLOCKSIZ];
    memset(block, 0, BLOCKSIZ);
    fseek(m_fd, 0, SEEK_SET);
    fwrite(block, BLOCKSIZ, 1, m_fd);

    char inode_block[DINODEBLK * BLOCKSIZ];
    memset(inode_block, 0, sizeof(inode_block));
    fseek(m_fd, DINODESTART, SEEK_SET);
    fwrite(inode_block, sizeof(inode_block), 1, m_fd);

    dinode root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.di_number = 1;
    root_inode.di_mode = DIDIR | DEFAULTMODE;
    root_inode.di_uid = 0;
    root_inode.di_gid = 0;
    root_inode.di_size = sizeof(direct) * 2;
    root_inode.di_addr[0] = 0;

    fseek(m_fd, DINODESTART + 1 * DINODESIZ, SEEK_SET);
    fwrite(&root_inode, sizeof(root_inode), 1, m_fd);

    memset(block, 0, BLOCKSIZ);
    direct* dir_entry = (direct*)block;
    strcpy(dir_entry[0].d_name, ".");
    dir_entry[0].d_ino = 1;
    strcpy(dir_entry[1].d_name, "..");
    dir_entry[1].d_ino = 1;

    fseek(m_fd, DATASTART + 0 * BLOCKSIZ, SEEK_SET);
    fwrite(block, BLOCKSIZ, 1, m_fd);

    dinode etc_inode;
    memset(&etc_inode, 0, sizeof(dinode));
    etc_inode.di_number = 1;
    etc_inode.di_mode = DIDIR | DEFAULTMODE;
    etc_inode.di_uid = 0;
    etc_inode.di_gid = 0;
    etc_inode.di_size = sizeof(direct) * 2;
    etc_inode.di_addr[0] = 1;

    fseek(m_fd, DINODESTART + 2 * DINODESIZ, SEEK_SET);
    fwrite(&etc_inode, sizeof(etc_inode), 1, m_fd);

    memset(block, 0, BLOCKSIZ);
    dir_entry = (direct*)block;
    strcpy(dir_entry[0].d_name, ".");
    dir_entry[0].d_ino = 2;
    strcpy(dir_entry[1].d_name, "..");
    dir_entry[1].d_ino = 1;

    fseek(m_fd, DATASTART + 1 * BLOCKSIZ, SEEK_SET);
    fwrite(block, BLOCKSIZ, 1, m_fd);

    dinode pwd_inode;
    memset(&pwd_inode, 0, sizeof(dinode));
    pwd_inode.di_number = 1;
    pwd_inode.di_mode = DIFILE | (UDIREAD | UDIWRITE);
    pwd_inode.di_uid = 0;
    pwd_inode.di_gid = 0;
    pwd_inode.di_size = sizeof(pwd) * PWDNUM;
    pwd_inode.di_addr[0] = 2;

    fseek(m_fd, DINODESTART + 3 * DINODESIZ, SEEK_SET);
    fwrite(&pwd_inode, sizeof(pwd_inode), 1, m_fd);

    memset(block, 0, BLOCKSIZ);
    pwd* pwd_entry = (pwd*)block;
    pwd_entry[0].p_uid = 0;
    pwd_entry[0].p_gid = 0;
    strcpy(pwd_entry[0].password, "root");
    pwd_entry[1].p_uid = 1;
    pwd_entry[1].p_gid = 1;
    strcpy(pwd_entry[1].password, "123456");

    fseek(m_fd, DATASTART + 2 * BLOCKSIZ, SEEK_SET);
    fwrite(block, BLOCKSIZ, 1, m_fd);

    int total_free = FILEBLK - 3;
    int free_blocks[FILEBLK];
    for (int i = 0; i < total_free; i++) {
        free_blocks[i] = 3 + i;
    }
    int pos = 0;
    superblock.s_nfree = NICFREE;
    superblock.s_pfree = NICFREE - 1;
    for (int i = 0; i < NICFREE; i++) {
        superblock.s_free[i] = free_blocks[pos++];
    }

    while (pos < total_free) {
        int prev_group_start = pos - NICFREE;
        if (prev_group_start < 0) {
            prev_group_start = 0;
        }
        int link_block = free_blocks[prev_group_start];
        int remaining = total_free - pos;
        int group_size = remaining < NICFREE ? remaining : NICFREE;
        char link_block_data[BLOCKSIZ];
        memset(link_block_data, 0, BLOCKSIZ);
        unsigned int* link_block_ptr = (unsigned int*)link_block_data;
        link_block_ptr[0] = (unsigned int)group_size;
        for (int i = 0; i < group_size; i++) {
            link_block_ptr[i + 1] = free_blocks[pos + i];
        }
        fseek(m_fd, DATASTART + link_block * BLOCKSIZ, SEEK_SET);
        fwrite(link_block_data, BLOCKSIZ, 1, m_fd);
        pos += group_size;
    }
    superblock.s_ninode = 0;
    for (unsigned int i = 511; i >= 4 && superblock.s_ninode < NICINOD; i--) {
        superblock.s_inode[superblock.s_ninode++] = i;
    }
    superblock.s_rinode = 4;
    superblock.s_pinode = superblock.s_ninode - 1;

    superblock.s_isize = DINODEBLK;
    superblock.s_fsize = FILEBLK;
    superblock.s_fmod = SUPDATE;

    fseek(m_fd, BLOCKSIZ, SEEK_SET);
    fwrite(&superblock, sizeof(superblock), 1, m_fd);

    fflush(m_fd);
    fclose(m_fd);
    m_fd = nullptr;

    printf("Format completed: virtual disk initialized.\n");
}

// ====== install() ======

void VirtualDisk::install(filsys& superblock)
{
    m_fd = fopen("filesystem", "r+b");
    if (!m_fd) {
        printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_IO));
        exit(1);
    }

    fseek(m_fd, BLOCKSIZ, SEEK_SET);
    fread(&superblock, sizeof(superblock), 1, m_fd);

    printf("Install completed: virtual disk loaded and ready.\n");
}
