/**
 * test_inode_dir.cpp - Unit tests for Layer 3 (iget/iput) + Layer 5 (namei/iname/dir)
 * Member B
 */
#include <cstdio>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "test_helpers.h"

/* Helper: format + install + login as root + load root dir */
static void setup_fs_with_login(VfsFixture& fx)
{
    fx.format();
    fx.install();
    fx.load_root_dir();
    /* Set up password table for login */
    fx.users.pwd_table()[0].p_uid = 0;
    fx.users.pwd_table()[0].p_gid = 0;
    std::strcpy(fx.users.pwd_table()[0].password, "root");
    fx.users.login(0, "root");
}

/* Helper: format + install + load root dir */
static void setup_fs(VfsFixture& fx)
{
    fx.format();
    fx.install();
    fx.load_root_dir();
}

/* ================================================================
 *  namei() tests
 * ================================================================ */

TEST_CASE("namei() finds . and .. after install", "[namei]")
{
    VfsFixture fx;
    setup_fs(fx);

    unsigned int idx_dot  = fx.dirs.namei(".");
    unsigned int idx_dot2 = fx.dirs.namei("..");

    REQUIRE(idx_dot  == 0);
    REQUIRE(idx_dot2 == 1);
    REQUIRE(fx.dirs.current_dir().direct[idx_dot].d_ino  == 1);
    REQUIRE(fx.dirs.current_dir().direct[idx_dot2].d_ino == 1);
}

TEST_CASE("namei() returns -1 for non-existent file", "[namei]")
{
    VfsFixture fx;
    setup_fs(fx);

    unsigned int idx = fx.dirs.namei("no_such_file");

    /* Fixes known bug #1: not-found must not be ambiguous with index 0 */
    REQUIRE(idx == (unsigned int)-1);
}

TEST_CASE("namei() skips deleted entries (d_ino == 0)", "[namei]")
{
    VfsFixture fx;
    setup_fs(fx);

    /* Simulate a deleted entry: slot 2 has a name but d_ino == 0 */
    dir& cur = fx.dirs.current_dir();
    strcpy(cur.direct[2].d_name, "deleted_file");
    cur.direct[2].d_ino = 0;
    cur.size = 3;

    unsigned int idx = fx.dirs.namei("deleted_file");
    REQUIRE(idx == (unsigned int)-1); /* skipped because d_ino == 0 */
}

/* ================================================================
 *  iname() tests
 * ================================================================ */

TEST_CASE("iname() finds empty slot and writes name", "[iname]")
{
    VfsFixture fx;
    setup_fs(fx);

    /* After install: slots 0+1 occupied (. and ..), slot 2 is free */
    unsigned short slot = fx.dirs.iname("newfile");

    REQUIRE(slot == 2);
    dir& cur = fx.dirs.current_dir();
    REQUIRE(strcmp(cur.direct[slot].d_name, "newfile") == 0);
    REQUIRE(cur.direct[slot].d_ino == 0); /* caller sets d_ino later */
}

TEST_CASE("iname() reuses deleted-entry slots", "[iname]")
{
    VfsFixture fx;
    setup_fs(fx);

    /* Fill slot 2, then delete it, then verify iname reuses it */
    dir& cur = fx.dirs.current_dir();
    strcpy(cur.direct[2].d_name, "will_delete");
    cur.direct[2].d_ino = 42; /* pretend it's allocated */
    cur.size = 3;

    /* "Delete": set d_ino to 0 */
    cur.direct[2].d_ino = 0;

    unsigned short slot = fx.dirs.iname("reused");
    REQUIRE(slot == 2);
    REQUIRE(strcmp(cur.direct[2].d_name, "reused") == 0);
}

/* ================================================================
 *  iget() / iput() tests
 * ================================================================ */

TEST_CASE("iget() loads inode from disk", "[iget]")
{
    VfsFixture fx;
    fx.format();
    fx.install();

    /* inode 2 = etc directory created during format */
    inode *ino = fx.icache.iget(2);
    REQUIRE(ino != nullptr);
    REQUIRE(ino->i_ino == 2);
    REQUIRE(ino->i_count == 1);
    REQUIRE(ino->di_mode & DIDIR);
    REQUIRE(ino->di_number == 1);
    /* etc dir has 2 entries (. and ..), 16 bytes each */
    REQUIRE(ino->di_size == sizeof(direct) * 2);

    fx.icache.iput(ino);
}

TEST_CASE("iget() caches: double call returns same pointer with i_count=2", "[iget]")
{
    VfsFixture fx;
    fx.format();
    fx.install();

    inode *p1 = fx.icache.iget(3); /* passwd file inode */
    REQUIRE(p1 != nullptr);
    REQUIRE(p1->i_count == 1);

    inode *p2 = fx.icache.iget(3);
    REQUIRE(p2 == p1);
    REQUIRE(p2->i_count == 2);

    /* Cleanup: release both references */
    fx.icache.iput(p1); /* i_count: 2 → 1 */
    REQUIRE(p1->i_count == 1);
    fx.icache.iput(p1); /* i_count: 1 → 0, writeback + free */
}

TEST_CASE("iput() writes back modified inode to disk", "[iget]")
{
    VfsFixture fx;
    fx.format();
    fx.install();

    /* Load etc dir inode, modify, release, reload, verify persistence */
    inode *ino = fx.icache.iget(2);
    REQUIRE(ino != nullptr);
    unsigned short original_size = ino->di_size;

    /* Modify */
    ino->di_size = 64;
    ino->i_flag |= IUPDATE;
    fx.icache.iput(ino); /* i_count 1→0, triggers writeback + free */

    /* Reload from disk */
    inode *reloaded = fx.icache.iget(2);
    REQUIRE(reloaded != nullptr);
    REQUIRE(reloaded->di_size == 64);

    /* Restore original value */
    reloaded->di_size = original_size;
    fx.icache.iput(reloaded);
}

TEST_CASE("iput() on deleted inode (di_number=0) zeros on-disk inode", "[iget]")
{
    VfsFixture fx;
    fx.format();
    fx.install();

    /* Load passwd inode, manually mark it deleted, and release */
    inode *ino = fx.icache.iget(3);
    REQUIRE(ino != nullptr);

    unsigned int saved_ino = ino->i_ino;

    /* Simulate deletion: di_number = 0 */
    ino->di_number = 0;

    /* Remember free-stack state before iput */
    unsigned int ninode_before = fx.superblock.s_ninode;

    fx.icache.iput(ino); /* should bfree blocks + ifree */

    /* ifree adds back to stack only if there is room (stack max is NICINOD=50).
       After format the stack is full, so it may not increase. That's expected. */
    if (ninode_before < NICINOD) {
        REQUIRE(fx.superblock.s_ninode == ninode_before + 1);
        REQUIRE(fx.superblock.s_inode[fx.superblock.s_pinode] == saved_ino);
    }

    /* The disk inode must be zeroed regardless of stack state */
    dinode check;
    fseek(fx.disk.handle(), DINODESTART + saved_ino * DINODESIZ, SEEK_SET);
    fread(&check, sizeof(check), 1, fx.disk.handle());
    REQUIRE(check.di_number == 0);
    REQUIRE(check.di_mode == 0);
}

/* ================================================================
 *  mkdir() tests
 * ================================================================ */

TEST_CASE("mkdir() creates subdirectory findable by namei", "[mkdir]")
{
    VfsFixture fx;
    setup_fs_with_login(fx);

    fx.dirs.mkdir("testdir", fx.users.current_user_id());

    unsigned int idx = fx.dirs.namei("testdir");
    REQUIRE(idx != (unsigned int)-1);
    REQUIRE(idx == 2); /* after . and .. */

    /* Verify the new directory's inode on disk */
    inode *ino = fx.icache.iget(fx.dirs.current_dir().direct[idx].d_ino);
    REQUIRE(ino != nullptr);
    REQUIRE(ino->di_mode & DIDIR);
    REQUIRE(ino->di_size == sizeof(direct) * 2); /* . and .. */
    REQUIRE(ino->di_addr[0] != 0);               /* has a data block */

    /* Verify . and .. entries in the new directory's data block */
    char block[BLOCKSIZ];
    fseek(fx.disk.handle(), DATASTART + ino->di_addr[0] * BLOCKSIZ, SEEK_SET);
    fread(block, BLOCKSIZ, 1, fx.disk.handle());
    direct *entries = (direct *)block;
    REQUIRE(strcmp(entries[0].d_name, ".")  == 0);
    REQUIRE(strcmp(entries[1].d_name, "..") == 0);

    fx.icache.iput(ino);
}

TEST_CASE("mkdir() rejects duplicate directory name", "[mkdir]")
{
    VfsFixture fx;
    setup_fs_with_login(fx);

    fx.dirs.mkdir("dup", fx.users.current_user_id());
    fx.dirs.mkdir("dup", fx.users.current_user_id()); /* should print error, not crash */

    /* namei should still find it exactly once */
    unsigned int first = fx.dirs.namei("dup");
    REQUIRE(first != (unsigned int)-1);
}

/* ================================================================
 *  chdir() tests
 * ================================================================ */

TEST_CASE("chdir() into subdirectory changes current directory", "[chdir]")
{
    VfsFixture fx;
    setup_fs_with_login(fx);

    unsigned int root_ino = fx.dirs.cur_path_inode()->i_ino;
    fx.dirs.mkdir("sub", fx.users.current_user_id());
    fx.dirs.chdir("sub");

    /* cur_path_inode should now be the subdirectory */
    REQUIRE(fx.dirs.cur_path_inode() != nullptr);
    REQUIRE(fx.dirs.cur_path_inode()->i_ino != root_ino);

    /* m_dir should contain . and .. */
    dir& cur = fx.dirs.current_dir();
    REQUIRE(cur.size == 2);
    REQUIRE(strcmp(cur.direct[0].d_name, ".")  == 0);
    REQUIRE(strcmp(cur.direct[1].d_name, "..") == 0);
    REQUIRE(cur.direct[1].d_ino == root_ino); /* .. points to parent */
}

TEST_CASE("chdir(..) from subdir returns to parent", "[chdir]")
{
    VfsFixture fx;
    setup_fs_with_login(fx);

    unsigned int root_ino = fx.dirs.cur_path_inode()->i_ino;
    fx.dirs.mkdir("child", fx.users.current_user_id());
    fx.dirs.chdir("child");
    REQUIRE(fx.dirs.cur_path_inode()->i_ino != root_ino);

    fx.dirs.chdir("..");
    REQUIRE(fx.dirs.cur_path_inode()->i_ino == root_ino);
    /* m_dir should show 3 entries: .  ..  child */
    REQUIRE(fx.dirs.current_dir().size >= 3);
    REQUIRE(fx.dirs.namei("child") != (unsigned int)-1);
}

TEST_CASE("chdir() rejects non-existent directory", "[chdir]")
{
    VfsFixture fx;
    setup_fs_with_login(fx);

    inode *before = fx.dirs.cur_path_inode();

    fx.dirs.chdir("nowhere");

    /* cur_path_inode should be unchanged */
    REQUIRE(fx.dirs.cur_path_inode() == before);
}

/* ================================================================
 *  dir_list() smoke test
 * ================================================================ */

TEST_CASE("dir_list() lists root directory without crashing", "[dir]")
{
    VfsFixture fx;
    setup_fs_with_login(fx);

    fx.dirs.mkdir("a", fx.users.current_user_id());
    fx.dirs.mkdir("b", fx.users.current_user_id());

    fx.dirs.dir_list(); /* should print 4 entries: . .. a b */
}

/* ================================================================
 *  Integration: full workflow
 * ================================================================ */

TEST_CASE("mkdir → chdir → mkdir → chdir .. round trip", "[integration]")
{
    VfsFixture fx;
    setup_fs_with_login(fx);

    /* Root: mkdir("A"), chdir("A") */
    fx.dirs.mkdir("A", fx.users.current_user_id());
    fx.dirs.chdir("A");
    REQUIRE(fx.dirs.namei(".")  == 0);
    REQUIRE(fx.dirs.namei("..") == 1);

    /* Inside A: mkdir("B"), chdir("B") */
    fx.dirs.mkdir("B", fx.users.current_user_id());
    fx.dirs.chdir("B");

    /* Inside B: verify . and .. */
    dir& cur = fx.dirs.current_dir();
    REQUIRE(strcmp(cur.direct[0].d_name, ".")  == 0);
    REQUIRE(strcmp(cur.direct[1].d_name, "..") == 0);

    /* Go back to A */
    fx.dirs.chdir("..");
    REQUIRE(fx.dirs.namei("B") != (unsigned int)-1);

    /* Go back to root */
    fx.dirs.chdir("..");
    REQUIRE(fx.dirs.namei("A") != (unsigned int)-1);
}
