#pragma once

#include "Core.h"

/**
 * VirtualDisk — Layer 1: Low-level virtual disk I/O
 *
 * Owns the host file handle (FILE*) and provides:
 *   - format()  — create a fresh virtual disk image on the host file
 *   - install() — mount an existing virtual disk, load superblock
 *   - Block-level read/write/seek helpers
 */
class VirtualDisk {
public:
    VirtualDisk() = default;

    // Non-copyable (owns FILE* resource)
    VirtualDisk(const VirtualDisk&) = delete;
    VirtualDisk& operator=(const VirtualDisk&) = delete;

    ~VirtualDisk();

    // -------- High-level lifecycle --------

    /** Create a fresh virtual disk image ("filesystem" host file).
     *  Initialises superblock, inode area, root dir, etc dir, pwd file,
     *  and the free-block / free-inode stacks. */
    void format(filsys& superblock);

    /** Mount an existing virtual disk image.
     *  Opens the host file, reads the superblock into `superblock`. */
    void install(filsys& superblock);

    // -------- Block-level I/O --------

    /** Read one block (512 B) from the virtual disk into `buffer`. */
    void read_block(uint32_t block_num, void* buffer);

    /** Write one block (512 B) from `buffer` to the virtual disk. */
    void write_block(uint32_t block_num, const void* buffer);

    /** Position the file pointer at `block_num * BLOCKSIZ + offset`. */
    void seek_block(uint32_t block_num, uint32_t offset = 0);

    // -------- Accessors --------

    FILE* handle() const { return m_fd; }
    bool  is_open() const { return m_fd != nullptr; }

private:
    FILE* m_fd = nullptr;
};
