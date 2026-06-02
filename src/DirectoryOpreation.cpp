/**
 * DirectoryOpreation.cpp — Layer 5: Directory operations
 */
#include <cstdio>
#include <cstring>

#include "Core.h"

#define DIRENTS_PER_BLOCK (BLOCKSIZ / sizeof(direct))

// ====== Constructor ======

DirectoryManager::DirectoryManager(VirtualDisk& disk, InodeCache& icache,
                                   BlockManager& blocks)
    : m_disk(disk), m_icache(icache), m_blocks(blocks)
{
}

// ====== Helpers ======

static void mode_str(unsigned short mode, char *out)
{
    out[0] = (mode & DIDIR) ? 'd' : '-';
    out[1] = (mode & UDIREAD)    ? 'r' : '-';
    out[2] = (mode & UDIWRITE)   ? 'w' : '-';
    out[3] = (mode & UDIEXECUTE) ? 'x' : '-';
    out[4] = (mode & GDIREAD)    ? 'r' : '-';
    out[5] = (mode & GDIWRITE)   ? 'w' : '-';
    out[6] = (mode & GDIEXECUTE) ? 'x' : '-';
    out[7] = (mode & ODIREAD)    ? 'r' : '-';
    out[8] = (mode & ODIWRITE)   ? 'w' : '-';
    out[9] = (mode & ODIEXECUTE) ? 'x' : '-';
    out[10] = '\0';
}

void DirectoryManager::writeback_dir(inode *ino)
{
    if (!ino) return;

    char block[BLOCKSIZ];
    memset(block, 0, BLOCKSIZ);

    int n = (int)m_dir.entries.size();
    if (n > (int)DIRENTS_PER_BLOCK) n = DIRENTS_PER_BLOCK;
    memcpy(block, m_dir.entries.data(), n * sizeof(direct));

    fseek(m_disk.handle(), DATASTART + ino->di_addr[0] * BLOCKSIZ, SEEK_SET);
    fwrite(block, BLOCKSIZ, 1, m_disk.handle());
    fflush(m_disk.handle());

    ino->di_size = n * sizeof(direct);
    ino->i_flag |= IUPDATE;

    // Update cache — the definitive copy lives here
    m_dir_cache[ino->i_ino] = m_dir.entries;
}

void DirectoryManager::load_dir(inode *ino)
{
    m_dir.entries.clear();
    if (!ino) return;

    // Check cache first — avoids disk-read of stale data
    auto it = m_dir_cache.find(ino->i_ino);
    if (it != m_dir_cache.end()) {
        m_dir.entries = it->second;
        return;
    }

    char block[BLOCKSIZ];
    memset(block, 0, BLOCKSIZ);
    fseek(m_disk.handle(), DATASTART + ino->di_addr[0] * BLOCKSIZ, SEEK_SET);
    fread(block, BLOCKSIZ, 1, m_disk.handle());

    int n = ino->di_size / sizeof(direct);
    if (n > (int)DIRENTS_PER_BLOCK) n = DIRENTS_PER_BLOCK;
    m_dir.entries.resize(n);
    memcpy(m_dir.entries.data(), block, n * sizeof(direct));

    m_dir_cache[ino->i_ino] = m_dir.entries;
}

// ====== namei() ======

unsigned int DirectoryManager::namei(const char *name)
{
    for (size_t i = 0; i < m_dir.entries.size(); i++) {
        if (strcmp(m_dir.entries[i].d_name, name) == 0
            && m_dir.entries[i].d_ino != 0) {
            return (unsigned int)i;
        }
    }
    return (unsigned int)-1;
}

// ====== iname() ======

unsigned short DirectoryManager::iname(const char *name)
{
    for (int i = 2; i < DIRNUM; i++) {    // skip . and ..
        if (i >= (int)m_dir.entries.size()) {
            m_dir.entries.resize(i + 1);
        }
        if (m_dir.entries[i].d_ino == 0) {
            strcpy(m_dir.entries[i].d_name, name);
            return (unsigned short)i;
        }
    }
    printf("Error: directory full (max %d entries).\n", DIRNUM);
    return 0;
}

// ====== dir_list() ======

void DirectoryManager::dir_list()
{
    printf("Directory listing  (size = %zu entries)\n", m_dir.entries.size());
    printf("%-16s %-10s %5s  %s\n", "Name", "Perms", "Size", "Blocks");

    for (size_t i = 0; i < m_dir.entries.size(); i++) {
        if (m_dir.entries[i].d_ino == 0) continue;

        inode *ino = m_icache.iget(m_dir.entries[i].d_ino);
        if (!ino) continue;

        char perm[11];
        mode_str(ino->di_mode, perm);

        if (ino->di_mode & DIDIR) {
            printf("%-16s %-10s %5s  <dir>\n",
                   m_dir.entries[i].d_name, perm, "-");
        } else {
            printf("%-16s %-10s %5u  ",
                   m_dir.entries[i].d_name, perm, ino->di_size);
            int blk_count = (ino->di_size + BLOCKSIZ - 1) / BLOCKSIZ;
            for (int b = 0; b < blk_count && b < NADDR; b++) {
                printf("%u ", ino->di_addr[b]);
            }
            printf("\n");
        }

        m_icache.iput(ino);
    }
}

// ====== mkdir() ======

void DirectoryManager::mkdir(const char *dirname, uint16_t)
{
    if (namei(dirname) != (unsigned int)-1) {
        printf("Error: '%s' already exists.\n", dirname);
        return;
    }

    unsigned short slot = iname(dirname);
    if (slot == 0 && !m_dir.entries.empty() && m_dir.entries[0].d_ino != 0) {
        return;
    }

    inode *ino = m_blocks.ialloc(m_icache);
    if (!ino) return;

    unsigned int blk = m_blocks.balloc();
    if (blk == DISKFULL) {
        printf("Error: disk full.\n");
        m_blocks.ifree(ino->i_ino);
        m_icache.iput(ino);
        return;
    }

    char block[BLOCKSIZ];
    memset(block, 0, BLOCKSIZ);
    direct *entry = (direct *)block;
    strcpy(entry[0].d_name, ".");
    entry[0].d_ino = ino->i_ino;
    strcpy(entry[1].d_name, "..");
    entry[1].d_ino = m_cur_path_inode ? m_cur_path_inode->i_ino : 1;

    fseek(m_disk.handle(), DATASTART + blk * BLOCKSIZ, SEEK_SET);
    fwrite(block, BLOCKSIZ, 1, m_disk.handle());
    fflush(m_disk.handle());
    fseek(m_disk.handle(), DATASTART + blk * BLOCKSIZ, SEEK_SET);

    ino->di_mode   = DIDIR | DEFAULTMODE;
    ino->di_uid    = 0;
    ino->di_gid    = 0;
    ino->di_size   = sizeof(direct) * 2;
    ino->di_addr[0] = blk;
    ino->i_flag   |= IUPDATE;

    if ((int)slot >= (int)m_dir.entries.size())
        m_dir.entries.resize(slot + 1);
    m_dir.entries[slot].d_ino = ino->i_ino;

    // Cache the new directory's entries (., ..) so load_dir won't hit disk
    std::vector<direct> new_entries = {
        direct{}, direct{}
    };
    strcpy(new_entries[0].d_name, ".");
    new_entries[0].d_ino = ino->i_ino;
    strcpy(new_entries[1].d_name, "..");
    new_entries[1].d_ino = m_cur_path_inode ? m_cur_path_inode->i_ino : 1;
    m_dir_cache[ino->i_ino] = std::move(new_entries);

    m_icache.iput(ino);
    printf("Directory '%s' created.\n", dirname);
}

// ====== chdir() ======

void DirectoryManager::chdir(const char *dirname)
{
    unsigned int idx = namei(dirname);
    if (idx == (unsigned int)-1) {
        printf("Error: '%s' not found.\n", dirname);
        return;
    }

    inode *target = m_icache.iget(m_dir.entries[idx].d_ino);
    if (!target) return;

    if (!(target->di_mode & DIDIR)) {
        printf("Error: '%s' is not a directory.\n", dirname);
        m_icache.iput(target);
        return;
    }

    writeback_dir(m_cur_path_inode);
    m_icache.iput(m_cur_path_inode);
    m_cur_path_inode = target;
    load_dir(target);
}
