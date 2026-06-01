#include <catch2/catch_test_macros.hpp>

#include "test_helpers.h"

TEST_CASE("balloc() allocates blocks in LIFO order", "[balloc]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    /* After format: s_free[0..49] = [3, 4, ..., 52], s_pfree = 49 */

    /* First allocation returns the top of the stack */
    uint32_t b1 = fx.blocks.balloc();
    REQUIRE(b1 == 3 + NICFREE - 1); /* block 52 */
    REQUIRE(fx.superblock.s_nfree == NICFREE - 1);
    REQUIRE(fx.superblock.s_pfree == NICFREE - 2);

    /* Second allocation */
    uint32_t b2 = fx.blocks.balloc();
    REQUIRE(b2 == 3 + NICFREE - 2); /* block 51 */
}

TEST_CASE("bfree() returns block to free stack", "[bfree]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    uint32_t b1 = fx.blocks.balloc(); /* allocate one block */
    uint32_t nfree_before = fx.superblock.s_nfree;
    uint32_t pfree_before = fx.superblock.s_pfree;

    fx.blocks.bfree(b1);
    REQUIRE(fx.superblock.s_nfree == nfree_before + 1);
    REQUIRE(fx.superblock.s_pfree == pfree_before + 1);
    REQUIRE(fx.superblock.s_free[fx.superblock.s_pfree] == b1);
}

TEST_CASE("bfree() spills to disk when stack is full", "[bfree]") {
    VfsFixture fx;
    fx.format();
    fx.install();

    /* Allocate blocks until s_nfree reaches 1, then one more to trigger group load. */
    for (int i = 0; i < 50; i++) {
        fx.blocks.balloc();
    }

    /* Fill the stack back up to NICFREE properly */
    for (uint16_t i = 0; i < NICFREE; i++) {
        fx.superblock.s_free[i] = 200 + i;
    }
    fx.superblock.s_nfree = NICFREE;
    fx.superblock.s_pfree = NICFREE - 1;
    REQUIRE(fx.superblock.s_nfree == NICFREE);

    /* Now bfree should spill: write current stack to disk, put block in slot 0 */
    uint32_t spill_block = 999;
    fx.blocks.bfree(spill_block);

    REQUIRE(fx.superblock.s_nfree == 1);
    REQUIRE(fx.superblock.s_pfree == 0);
    REQUIRE(fx.superblock.s_free[0] == spill_block);
}
