#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

/**
 * Shell — Layer 7: Command REPL (filename-based)
 *
 * All file operations use filenames, not raw fd numbers.
 * An internal path→fd map translates names to descriptors.
 */
class Shell {
public:
    /** Enter the command loop.  Reads from stdin until EOF or halt/exit/quit. */
    static void run();

private:
    /** Map filename → user file descriptor. */
    static std::unordered_map<std::string, int> s_path2fd;

    /** Look up a filename in the map.  Returns -1 if not found. */
    static int lookup_fd(const std::string& name);

    /** Store a filename→fd mapping (replaces any existing entry). */
    static void bind_fd(const std::string& name, int fd);

    /** Remove a filename→fd mapping. */
    static void unbind_fd(const std::string& name);

    /** Read the full size of the file backing `fd`. */
    static uint32_t file_size(int fd);

    /** Editor mode: read lines from stdin until blank line, return joined. */
    static std::string editor_read();
};
