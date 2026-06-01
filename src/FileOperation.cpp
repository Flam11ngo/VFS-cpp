/**
 * FileOperation.cpp — Layer 4: File operations and access control
 * access / creat / open / close / delete_file / read / write
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Core.h"

// ====== Constructor ======

FileOperator::FileOperator(VirtualDisk& disk, BlockManager& blocks,
                           InodeCache& icache, DirectoryManager& dirs,
                           filsys& superblock)
    : m_disk(disk), m_blocks(blocks), m_icache(icache),
      m_dirs(dirs), m_superblock(superblock)
{
    for (int i = 0; i < SYSOPENFILE; i++) {
        m_ofile[i].f_count = 0;
        m_ofile[i].f_inode = nullptr;
        m_ofile[i].f_flag = 0;
        m_ofile[i].f_off = 0;
    }
}

// ====== access() ======

unsigned int FileOperator::access(const user_t& user, inode *inode,
                                  unsigned short mode)
{
    if (user.u_uid == 0)
        return 1;

    unsigned short perm_mask = 0;

    if (user.u_uid == inode->di_uid) {
        switch (mode) {
        case READ_MODE:    perm_mask = UDIREAD;    break;
        case WRITE_MODE:   perm_mask = UDIWRITE;   break;
        case EXECUTE_MODE: perm_mask = UDIEXECUTE; break;
        default: return 0;
        }
    } else if (user.u_gid == inode->di_gid) {
        switch (mode) {
        case READ_MODE:    perm_mask = GDIREAD;    break;
        case WRITE_MODE:   perm_mask = GDIWRITE;   break;
        case EXECUTE_MODE: perm_mask = GDIEXECUTE; break;
        default: return 0;
        }
    } else {
        switch (mode) {
        case READ_MODE:    perm_mask = ODIREAD;    break;
        case WRITE_MODE:   perm_mask = ODIWRITE;   break;
        case EXECUTE_MODE: perm_mask = ODIEXECUTE; break;
        default: return 0;
        }
    }

    return (inode->di_mode & perm_mask) ? 1 : 0;
}

// ====== creat() ======

int FileOperator::creat(user_t& user, const char *filename, unsigned short mode)
{
    inode *ino = nullptr;

    unsigned int di = m_dirs.namei(filename);

    if (di < DIRNUM && m_dirs.current_dir().entries[di].d_ino != DIEMPTY) {
        /* File exists → truncate */
        ino = m_icache.iget(m_dirs.current_dir().entries[di].d_ino);
        if (!ino) return -1;

        if (!access(user, ino, WRITE_MODE)) {
            m_icache.iput(ino);
            return -1;
        }

        for (int i = 0; i < NADDR; i++) {
            if (ino->di_addr[i] != 0) {
                m_blocks.bfree(ino->di_addr[i]);
                ino->di_addr[i] = 0;
            }
        }
        ino->di_size = 0;
        ino->i_flag |= IUPDATE;

        for (int i = 0; i < SYSOPENFILE; i++) {
            if (m_ofile[i].f_inode == ino) {
                m_ofile[i].f_off = 0;
            }
        }
    } else {
        /* File does not exist → create new */
        unsigned short slot = m_dirs.iname(filename);
        if (slot >= DIRNUM) return -1;

        ino = m_blocks.ialloc(m_icache);
        if (!ino) return -1;

        ino->di_mode  = DIFILE | (mode & DEFAULTMODE);
        ino->di_uid   = user.u_uid;
        ino->di_gid   = user.u_gid;
        ino->di_size  = 0;
        ino->i_flag  |= IUPDATE;

        m_dirs.current_dir().entries[slot].d_ino = ino->i_ino;
    }

    /* Allocate system open-file-table slot */
    int sys_no = -1;
    for (int i = 1; i < SYSOPENFILE; i++) {
        if (m_ofile[i].f_count == 0) { sys_no = i; break; }
    }
    if (sys_no == -1) { m_icache.iput(ino); return -1; }

    /* Allocate user open-file-table slot */
    int user_fd = -1;
    for (int j = 0; j < NOFILE; j++) {
        if (user.u_ofile[j] == SYSOPENFILE + 1) { user_fd = j; break; }
    }
    if (user_fd == -1) { m_icache.iput(ino); return -1; }

    /* Link everything together */
    m_ofile[sys_no].f_flag  = FWRITE;
    m_ofile[sys_no].f_count = 1;
    m_ofile[sys_no].f_inode = ino;
    m_ofile[sys_no].f_off   = 0;

    user.u_ofile[user_fd] = sys_no;

    return user_fd;
}

// ====== open() ======

unsigned short FileOperator::open(user_t& user, const char *filename,
                                  unsigned short openmode)
{
    unsigned int di = m_dirs.namei(filename);

    if (di >= DIRNUM || m_dirs.current_dir().entries[di].d_ino == DIEMPTY) {
        return (unsigned short)-1;
    }

    inode *ino = m_icache.iget(m_dirs.current_dir().entries[di].d_ino);
    if (!ino) return (unsigned short)-1;

    if (openmode & FREAD) {
        if (!access(user, ino, READ_MODE)) {
            m_icache.iput(ino);
            return (unsigned short)-1;
        }
    }
    if (openmode & (FWRITE | FAPPEND)) {
        if (!access(user, ino, WRITE_MODE)) {
            m_icache.iput(ino);
            return (unsigned short)-1;
        }
    }

    int sys_no = -1;
    for (int i = 1; i < SYSOPENFILE; i++) {
        if (m_ofile[i].f_count == 0) { sys_no = i; break; }
    }
    if (sys_no == -1) { m_icache.iput(ino); return (unsigned short)-1; }

    int user_fd = -1;
    for (int j = 0; j < NOFILE; j++) {
        if (user.u_ofile[j] == SYSOPENFILE + 1) { user_fd = j; break; }
    }
    if (user_fd == -1) { m_icache.iput(ino); return (unsigned short)-1; }

    m_ofile[sys_no].f_flag  = openmode;
    m_ofile[sys_no].f_count = 1;
    m_ofile[sys_no].f_inode = ino;

    if (openmode & FAPPEND) {
        m_ofile[sys_no].f_off = ino->di_size;
    } else {
        m_ofile[sys_no].f_off = 0;
    }

    user.u_ofile[user_fd] = sys_no;

    return (unsigned short)user_fd;
}

// ====== close() ======

void FileOperator::close(user_t& user, unsigned short cfd)
{
    if (cfd >= NOFILE) return;

    unsigned short sys_no = user.u_ofile[cfd];
    if (sys_no >= SYSOPENFILE) return;

    inode *ino = m_ofile[sys_no].f_inode;
    if (ino) m_icache.iput(ino);

    if (m_ofile[sys_no].f_count > 0) {
        m_ofile[sys_no].f_count--;
    }

    user.u_ofile[cfd] = SYSOPENFILE + 1;
}

// ====== delete_file() ======

void FileOperator::delete_file(const char *filename)
{
    unsigned int di = m_dirs.namei(filename);

    if (di >= DIRNUM || m_dirs.current_dir().entries[di].d_ino == DIEMPTY) {
        printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_NOENT));
        return;
    }

    inode *ino = m_icache.iget(m_dirs.current_dir().entries[di].d_ino);
    if (!ino) {
        printf("Error: %s\n", vfs_strerror(VfsError::E_VFS_IO));
        return;
    }

    if (ino->di_number > 0) {
        ino->di_number--;
    }
    ino->i_flag |= IUPDATE;

    if (ino->di_number == 0) {
        m_dirs.current_dir().entries[di].d_ino = DIEMPTY;
        dir& curdir = m_dirs.current_dir();
        curdir.entries.erase(curdir.entries.begin() + di);
    }

    m_icache.iput(ino);
}

// ====== read() ======

unsigned int FileOperator::read(user_t& user, unsigned int fd,
                                char *buf, unsigned int size)
{
    if (fd >= NOFILE) return 0;

    unsigned short sys_no = user.u_ofile[fd];
    if (sys_no >= SYSOPENFILE) return 0;

    if (!(m_ofile[sys_no].f_flag & FREAD)) return 0;

    inode *ino = m_ofile[sys_no].f_inode;
    if (!ino) return 0;

    unsigned int off = m_ofile[sys_no].f_off;
    if (off >= ino->di_size) return 0;
    if (off + size > ino->di_size) size = ino->di_size - off;

    unsigned int remaining = size;
    unsigned int total_read = 0;
    char *buf_ptr = buf;

    while (remaining > 0) {
        unsigned int block_index = off / BLOCKSIZ;
        unsigned int block_offset = off % BLOCKSIZ;

        if (block_index >= NADDR) break;
        unsigned int block_num = ino->di_addr[block_index];
        if (block_num == 0) break;

        unsigned int chunk = BLOCKSIZ - block_offset;
        if (chunk > remaining) chunk = remaining;

        if (fseek(m_disk.handle(), DATASTART + block_num * BLOCKSIZ + block_offset, SEEK_SET) != 0)
            break;
        size_t n = fread(buf_ptr, 1, chunk, m_disk.handle());
        if (n == 0) break;

        buf_ptr    += n;
        off        += (unsigned int)n;
        remaining  -= (unsigned int)n;
        total_read += (unsigned int)n;

        if (n < chunk) { remaining = 0; }
    }

    m_ofile[sys_no].f_off = off;
    return total_read;
}

// ====== write() ======

unsigned int FileOperator::write(user_t& user, unsigned int fd,
                                 const char *buf, unsigned int size)
{
    if (fd >= NOFILE) return 0;

    unsigned short sys_no = user.u_ofile[fd];
    if (sys_no >= SYSOPENFILE) return 0;

    if (!(m_ofile[sys_no].f_flag & (FWRITE | FAPPEND))) return 0;

    inode *ino = m_ofile[sys_no].f_inode;
    if (!ino) return 0;

    unsigned int off = m_ofile[sys_no].f_off;
    unsigned int remaining = size;
    unsigned int total_written = 0;
    const char *buf_ptr = buf;

    while (remaining > 0) {
        unsigned int block_index = off / BLOCKSIZ;
        unsigned int block_offset = off % BLOCKSIZ;

        if (block_index >= NADDR) break;

        unsigned int block_num = ino->di_addr[block_index];
        if (block_num == 0) {
            block_num = m_blocks.balloc();
            if (block_num == DISKFULL) break;
            ino->di_addr[block_index] = block_num;
            ino->i_flag |= IUPDATE;
        }

        unsigned int chunk = BLOCKSIZ - block_offset;
        if (chunk > remaining) chunk = remaining;

        if (chunk < BLOCKSIZ) {
            char temp[BLOCKSIZ];
            memset(temp, 0, BLOCKSIZ);
            fseek(m_disk.handle(), DATASTART + block_num * BLOCKSIZ, SEEK_SET);
            fread(temp, BLOCKSIZ, 1, m_disk.handle());
            memcpy(temp + block_offset, buf_ptr, chunk);
            fseek(m_disk.handle(), DATASTART + block_num * BLOCKSIZ, SEEK_SET);
            if (fwrite(temp, BLOCKSIZ, 1, m_disk.handle()) != 1) break;
        } else {
            fseek(m_disk.handle(), DATASTART + block_num * BLOCKSIZ, SEEK_SET);
            if (fwrite(buf_ptr, chunk, 1, m_disk.handle()) != 1) break;
        }

        buf_ptr       += chunk;
        off           += chunk;
        remaining     -= chunk;
        total_written += chunk;
    }

    if (off > ino->di_size) {
        ino->di_size = off;
        ino->i_flag |= IUPDATE;
    }
    m_ofile[sys_no].f_off = off;
    fflush(m_disk.handle());

    return total_written;
}
