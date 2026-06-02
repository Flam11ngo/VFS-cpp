#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

/**
 * Shell — Layer 7: Command REPL (filename-based)
 *
 * All file operations use filenames, not raw fd numbers.
 * An internal path→fd map translates names to descriptors.
 * Paths support subdirectories (e.g. "a/b/f.txt") with auto-mkdir.
 */
class Shell {
public:
    /** Enter the command loop.  Reads from stdin until EOF or halt/exit/quit. */
    static void run();

    /** Read the full size of the file backing `fd` (0 on error). */
    static uint32_t file_size(int fd);

    /** Current working directory as a human-readable path string. */
    static std::string s_cwd;

private:
    /** Map full path → user file descriptor. */
    static std::unordered_map<std::string, int> s_path2fd;

    /** Look up a path in the map.  Returns -1 if not found. */
    static int  lookup_fd(const std::string& path);
    static void bind_fd(const std::string& path, int fd);
    static void unbind_fd(const std::string& path);

    /** Editor mode: read lines from stdin until blank line, return joined. */
    static std::string editor_read();
    static std::string editor_read(const std::string& existing);

    /** Print the shell prompt (cwd + "$ "). */
    static void show_prompt();
};
