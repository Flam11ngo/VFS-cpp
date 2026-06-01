/**
 * Shell.cpp — Filename-based command REPL
 *
 * Every file operation uses filenames.  An internal path→fd hash table
 * translates names to descriptors transparently.
 *
 * Defaults:
 *   creat / open   mode  →  0777 / FREAD
 *   read           size  →  whole file
 *   write                 →  interactive editor mode
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>

#include "Core.h"

// ====== Static members ======

std::unordered_map<std::string, int> Shell::s_path2fd;

int Shell::lookup_fd(const std::string& name)
{
    auto it = s_path2fd.find(name);
    return (it != s_path2fd.end()) ? it->second : -1;
}

void Shell::bind_fd(const std::string& name, int fd)
{
    s_path2fd[name] = fd;
}

void Shell::unbind_fd(const std::string& name)
{
    s_path2fd.erase(name);
}

uint32_t Shell::file_size(int fd)
{
    auto& files = g_core.files();
    auto& users = g_core.users();
    if (fd < 0 || fd >= NOFILE) return 0;
    unsigned short sys_no = users.current_user().u_ofile[fd];
    if (sys_no >= SYSOPENFILE) return 0;
    inode* ino = files.ofile_table()[sys_no].f_inode;
    return ino ? ino->di_size : 0;
}

std::string Shell::editor_read()
{
    return editor_read("");
}

std::string Shell::editor_read(const std::string& existing)
{
    // Display existing content (read-only, for reference)
    int ln = 1;
    if (!existing.empty()) {
        std::cout << "  --- file content ---\n";
        std::istringstream iss(existing);
        std::string line;
        while (std::getline(iss, line))
            std::cout << "  " << ln++ << "| " << line << "\n";
    }
    // Read new input only — existing is NOT included in result
    std::cout << "  (enter text, blank line to finish)\n";
    std::string buf, line;
    while (true) {
        std::cout << "  " << ln++ << "| ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) break;
        if (!buf.empty()) buf += '\n';
        buf += line;
    }
    return buf;
}

// ====== Path helpers ======

/** Split a path like "a/b/c/f.txt" into directory components + final filename.
 *  Auto-creates missing directories via mkdir.  Changes cwd along the way.
 *  Returns the filename (last component).  Caller must chdir("..") depth times
 *  to restore the original directory. */
static std::pair<std::string, int> resolve_path(const std::string& path)
{
    auto& dirs = g_core.dirs();

    // Split by '/'
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string token;
    while (std::getline(iss, token, '/'))
        parts.push_back(token);

    if (parts.empty()) return {"", 0};

    int depth = 0;
    // All components except the last are directories
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        if (dirs.namei(parts[i].c_str()) == (unsigned int)-1) {
            // Directory doesn't exist — create it
            dirs.mkdir(parts[i].c_str(), 0);
        }
        dirs.chdir(parts[i].c_str());
        depth++;
    }

    return {parts.back(), depth};
}

/** chdir("..") depth times to undo resolve_path. */
static void unwind_path(int depth)
{
    for (int i = 0; i < depth; i++)
        g_core.dirs().chdir("..");
}

/** Open, read entire content, close.  Returns empty string on failure. */
static std::string read_entire_file(const std::string& name)
{
    auto& files = g_core.files();
    auto& users = g_core.users();

    uint16_t fd = files.open(users.current_user(), name.c_str(), FREAD);
    if (fd == static_cast<uint16_t>(-1)) return {};

    uint32_t size = Shell::file_size(fd);
    if (size == 0) { files.close(users.current_user(), fd); return {}; }

    char* buf = new char[size + 1];
    memset(buf, 0, size + 1);
    uint32_t n = files.read(users.current_user(), fd, buf, size);
    std::string result(buf, n);
    delete[] buf;
    files.close(users.current_user(), fd);
    return result;
}

// ====== Helpers ======

static std::vector<std::string> parse_cmd(const std::string &line)
{
    std::vector<std::string> args;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) args.push_back(token);
    return args;
}

static void show_help()
{
    std::cout
        << "Commands:\n"
        << "  login  <uid> <passwd>       Log in\n"
        << "  logout                       Log out\n"
        << "  ls | dir                     List current directory\n"
        << "  mkdir <name>                 Create subdirectory\n"
        << "  cd    <name>                 Change directory\n"
        << "  creat <name> [mode]          Create file  (default mode 0777)\n"
        << "  open  <name> [mode]          Open file    (default mode 1=read)\n"
        << "  close <name>                 Close file\n"
        << "  read  <name> [size]          Read file   (default: all bytes)\n"
        << "  write [-o] <name> [text]     Append / overwrite file\n"
        << "  delete <name>                Delete file\n"
        << "  format                       Format disk\n"
        << "  halt | exit | quit           Shutdown\n";
}

static bool parse_uint(const std::string &text, unsigned long &value)
{
    try {
        value = std::stoul(text);
        return true;
    } catch (const std::exception &) {
        std::cout << "  Invalid number: " << text << '\n';
        return false;
    }
}

// ====== run() ======

void Shell::run()
{
    auto& dirs  = g_core.dirs();
    auto& files = g_core.files();
    auto& users = g_core.users();
    auto& disk  = g_core.disk();
    auto& blocks = g_core.blocks();
    auto& icache = g_core.icache();

    std::string line;
    while (true) {
        std::cout << "$ ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        auto args = parse_cmd(line);
        if (args.empty()) continue;

        const std::string &cmd = args[0];

        // ============================================================
        //  help
        // ============================================================
        if (cmd == "help") {
            show_help();
        }

        // ============================================================
        //  login  <uid>  <password>
        // ============================================================
        else if (cmd == "login") {
            if (args.size() < 3) {
                std::cout << "Usage: login <uid> <password>\n";
                continue;
            }
            unsigned long uid = 0;
            if (!parse_uint(args[1], uid)) continue;
            if (users.login(static_cast<uint16_t>(uid), args[2].c_str()))
                std::cout << "  Login successful (uid=" << uid << ").\n";
            else
                std::cout << "  Login failed.\n";
        }

        // ============================================================
        //  logout
        // ============================================================
        else if (cmd == "logout") {
            if (users.current_user_id() < 0) {
                std::cout << "  Not logged in.\n";
                continue;
            }
            unsigned short cur = users.current_user().u_uid;
            if (users.logout(cur)) {
                s_path2fd.clear();
                std::cout << "  Logged out (uid=" << cur << ").\n";
            } else {
                std::cout << "  Logout failed.\n";
            }
        }

        // ============================================================
        //  ls  |  dir
        // ============================================================
        else if (cmd == "dir" || cmd == "ls") {
            dirs.dir_list();
        }

        // ============================================================
        //  mkdir  <name>
        // ============================================================
        else if (cmd == "mkdir") {
            if (args.size() < 2) {
                std::cout << "Usage: mkdir <dirname>\n";
                continue;
            }
            dirs.mkdir(args[1].c_str(), users.current_user_id());
        }

        // ============================================================
        //  cd  <name>
        // ============================================================
        else if (cmd == "cd") {
            if (args.size() < 2) {
                std::cout << "Usage: cd <dirname>\n";
                continue;
            }
            dirs.chdir(args[1].c_str());
        }

        // ============================================================
        //  creat  <name>  [mode]
        // ============================================================
        else if (cmd == "creat") {
            if (args.size() < 2) {
                std::cout << "Usage: creat <filename> [mode]\n";
                continue;
            }
            unsigned long mode = DEFAULTMODE;
            if (args.size() >= 3 && !parse_uint(args[2], mode)) continue;

            auto [fname, depth] = resolve_path(args[1]);
            int fd = files.creat(users.current_user(), fname.c_str(),
                                 static_cast<uint16_t>(mode));
            if (fd >= 0) {
                bind_fd(fname, fd);
                std::cout << "  Created '" << fname << "'  fd=" << fd
                          << "  mode=" << std::oct << mode << std::dec << "\n";
            } else {
                std::cout << "  creat failed.\n";
            }
            unwind_path(depth);
        }

        // ============================================================
        //  open  <name>  [mode]
        // ============================================================
        else if (cmd == "open") {
            if (args.size() < 2) {
                std::cout << "Usage: open <filename> [mode]\n";
                continue;
            }
            unsigned long mode = FREAD;
            if (args.size() >= 3 && !parse_uint(args[2], mode)) continue;

            uint16_t fd = files.open(users.current_user(), args[1].c_str(),
                                     static_cast<uint16_t>(mode));
            if (fd != static_cast<uint16_t>(-1)) {
                bind_fd(args[1], fd);
                std::cout << "  Opened '" << args[1] << "'  fd=" << fd
                          << "  mode=" << std::oct << mode << std::dec << "\n";
            } else {
                std::cout << "  open failed: file not found or permission denied.\n";
            }
        }

        // ============================================================
        //  close  <name>
        // ============================================================
        else if (cmd == "close") {
            if (args.size() < 2) {
                std::cout << "Usage: close <filename>\n";
                continue;
            }
            int fd = lookup_fd(args[1]);
            if (fd < 0) {
                std::cout << "  '" << args[1] << "' is not open.\n";
                continue;
            }
            files.close(users.current_user(), static_cast<uint16_t>(fd));
            unbind_fd(args[1]);
            std::cout << "  Closed '" << args[1] << "'.\n";
        }

        // ============================================================
        //  read  <name>  [size]
        // ============================================================
        else if (cmd == "read") {
            if (args.size() < 2) {
                std::cout << "Usage: read <filename> [size]\n";
                continue;
            }
            auto [fname, depth] = resolve_path(args[1]);

            int fd = lookup_fd(fname);
            if (fd < 0) {
                uint16_t new_fd = files.open(users.current_user(), fname.c_str(), FREAD);
                if (new_fd == static_cast<uint16_t>(-1)) {
                    std::cout << "  Cannot open '" << fname << "' for reading.\n";
                    unwind_path(depth);
                    continue;
                }
                fd = new_fd;
                bind_fd(fname, fd);
                std::cout << "  (auto-opened '" << fname << "' fd=" << fd << ")\n";
            }

            uint32_t max_size = file_size(fd);
            if (args.size() >= 3) {
                unsigned long s = 0;
                if (!parse_uint(args[2], s)) {
                    files.close(users.current_user(), static_cast<uint16_t>(fd));
                    unbind_fd(fname);
                    unwind_path(depth);
                    continue;
                }
                max_size = static_cast<uint32_t>(s);
            }

            if (max_size == 0) {
                std::cout << "  (empty file)\n";
                files.close(users.current_user(), static_cast<uint16_t>(fd));
                unbind_fd(fname);
                unwind_path(depth);
                continue;
            }

            std::string content;
            content.reserve(max_size);
            uint32_t total_read = 0;
            while (total_read < max_size) {
                char chunk[BLOCKSIZ];
                uint32_t ask = max_size - total_read;
                if (ask > BLOCKSIZ) ask = BLOCKSIZ;
                uint32_t n = files.read(users.current_user(),
                                        static_cast<uint32_t>(fd), chunk, ask);
                if (n == 0) break;
                content.append(chunk, n);
                total_read += n;
            }

            std::cout << "  \n";
            std::istringstream iss(content);
            std::string line;
            int ln = 1;
            while (std::getline(iss, line))
                std::cout << "  " << ln++ << "| " << line << "\n";
            if (!content.empty() && content.back() == '\n')
                std::cout << "  " << ln++ << "| \n";
            std::cout << "  \n";

            files.close(users.current_user(), static_cast<uint16_t>(fd));
            unbind_fd(fname);
            unwind_path(depth);
        }

        // ============================================================
        //  write  [-o]  <name>  [text ...]
        // ============================================================
        else if (cmd == "write") {
            // Parse flags
            bool overwrite = false;
            size_t name_idx = 1;
            if (args.size() >= 2 && (args[1] == "-o" || args[1] == "--overwrite")) {
                overwrite = true;
                name_idx = 2;
            }

            if (args.size() <= name_idx) {
                std::cout << "Usage: write [-o|--overwrite] <filename> [text ...]\n"
                          << "  Default is append.  -o truncates before writing.\n";
                continue;
            }

            auto [fname, depth] = resolve_path(args[name_idx]);
            int fd = lookup_fd(fname);

            if (overwrite) {
                // Overwrite mode: close if open, creat (truncates), rebind
                if (fd >= 0) {
                    files.close(users.current_user(), static_cast<uint16_t>(fd));
                    unbind_fd(fname);
                }
                int cfd = files.creat(users.current_user(), fname.c_str(), DEFAULTMODE);
                if (cfd < 0) {
                    std::cout << "  Cannot overwrite '" << fname << "'.\n";
                    continue;
                }
                fd = cfd;
                bind_fd(fname, fd);
                std::cout << "  (overwrite mode, fd=" << fd << ")\n";
            } else {
                // Append mode: always open fresh with FAPPEND
                if (fd >= 0) {
                    files.close(users.current_user(), static_cast<uint16_t>(fd));
                    unbind_fd(fname);
                }
                uint16_t new_fd = files.open(users.current_user(), fname.c_str(),
                                             FWRITE | FAPPEND);
                if (new_fd == static_cast<uint16_t>(-1)) {
                    int cfd = files.creat(users.current_user(), fname.c_str(), DEFAULTMODE);
                    if (cfd < 0) {
                        std::cout << "  Cannot open or create '" << fname << "'.\n";
                        continue;
                    }
                    new_fd = static_cast<uint16_t>(cfd);
                }
                fd = new_fd;
                bind_fd(fname, fd);
            }

            // Gather text
            std::string text;
            size_t text_start = name_idx + 1;
            bool use_editor = (args.size() <= text_start);

            if (use_editor) {
                if (!overwrite) {
                    text = editor_read(read_entire_file(fname));
                } else {
                    text = editor_read();
                }
            } else {
                for (size_t i = text_start; i < args.size(); i++) {
                    if (i > text_start) text += ' ';
                    text += args[i];
                }
            }

            // Ensure newline gap when appending to a file without trailing \n
            if (!overwrite && !text.empty()) {
                std::string existing = read_entire_file(fname);
                if (!existing.empty() && existing.back() != '\n')
                    text.insert(0, "\n");
            }

            uint32_t n = files.write(users.current_user(),
                                     static_cast<uint32_t>(fd),
                                     text.c_str(),
                                     static_cast<uint32_t>(text.size()));
            std::cout << "  Written " << n << " bytes to '" << fname << "'.\n";

            // Auto-close after write
            files.close(users.current_user(), static_cast<uint16_t>(fd));
            unbind_fd(fname);
        }

        // ============================================================
        //  delete  <name>
        // ============================================================
        else if (cmd == "delete") {
            if (args.size() < 2) {
                std::cout << "Usage: delete <filename>\n";
                continue;
            }
            int fd = lookup_fd(args[1]);
            if (fd >= 0) {
                files.close(users.current_user(), static_cast<uint16_t>(fd));
                unbind_fd(args[1]);
            }
            files.delete_file(args[1].c_str());
            std::cout << "  Deleted '" << args[1] << "'.\n";
        }

        // ============================================================
        //  format
        // ============================================================
        else if (cmd == "format") {
            std::cout << "  Formatting virtual disk...\n";
            s_path2fd.clear();
            disk.format(blocks.superblock());
            icache.iput(dirs.cur_path_inode());
            dirs.set_cur_path_inode(nullptr);
            disk.install(blocks.superblock());
            std::cout << "  Format complete.  Please login again.\n";
        }

        // ============================================================
        //  halt  |  exit  |  quit
        // ============================================================
        else if (cmd == "halt" || cmd == "exit" || cmd == "quit") {
            g_core.exit();
            break;
        }

        // ============================================================
        //  unknown
        // ============================================================
        else {
            std::cout << "  Unknown command: " << cmd
                      << "\n  Type 'help' for available commands.\n";
        }
    }
}
