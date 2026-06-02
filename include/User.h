#pragma once

#include "Core.h"

class VirtualDisk;
class InodeCache;
class DirectoryManager;
class FileOperator;

/**
 * UserManager — Layer 6: User authentication and session management
 *
 * Owns the user table (user_t[USERNUM]), password table (pwd[PWDNUM]),
 * and current-user tracking. Provides login / logout / halt.
 */
class UserManager {
public:
    UserManager(VirtualDisk& disk, InodeCache& icache,
                DirectoryManager& dirs, FileOperator& files,
                filsys& superblock);

    // -------- Initialization --------

    /** Load password table from the virtual disk (inode 3).
     *  Must be called after VirtualDisk::install(). */
    void load_password_file();

    /** Persist password table back to disk (inode 3). */
    void save_password_file();

    // -------- Authentication --------

    /** Authenticate `uid` with `passwd`. On success, fills a user slot
     *  and returns 1. Returns 0 on failure (sets g_vfs_errno). */
    int32_t login(uint16_t uid, const char* passwd);

    /** Log out `uid` — closes all open files and clears the slot.
     *  Returns 1 on success, 0 on failure. */
    int32_t logout(uint16_t uid);

    // -------- System lifecycle --------

    /** Graceful shutdown: write back current directory, log out all users,
     *  write superblock to disk, close the virtual disk, exit. */
    void halt();

    // -------- Accessors --------

    int32_t current_user_id() const { return m_user_id; }
    void    set_user_id(int32_t id)  { m_user_id = id; }

    user_t& current_user()           { return m_users[m_user_id]; }
    user_t& user_at(int idx)         { return m_users[idx]; }
    pwd*    pwd_table()              { return m_pwd; }

private:
    VirtualDisk&      m_disk;
    InodeCache&       m_icache;
    DirectoryManager& m_dirs;
    FileOperator&     m_files;
    filsys&           m_superblock;

    user_t  m_users[USERNUM]{};
    pwd     m_pwd[PWDNUM]{};
    int32_t m_user_id = -1;
};
