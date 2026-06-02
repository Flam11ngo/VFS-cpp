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
std::string Shell::s_cwd = "/";

void Shell::show_prompt()
{
    std::cout << s_cwd << " $ ";
}

int Shell::lookup_fd(const std::string& path)
{
    auto it = s_path2fd.find(path);
    return (it != s_path2fd.end()) ? it->second : -1;
}

void Shell::bind_fd(const std::string& path, int fd)
{
    s_path2fd[path] = fd;
}

void Shell::unbind_fd(const std::string& path)
{
    s_path2fd.erase(path);
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
    return Editor::run("");
}

std::string Shell::editor_read(const std::string& existing)
{
    return Editor::run(existing);
}

// ====== Path helpers ======

static void unwind_path(int depth);   // forward decl

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
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        if (parts[i] == "..") {
            if (depth > 0) {
                g_core.dirs().chdir("..");
                depth--;
                auto pos = Shell::s_cwd.rfind('/');
                if (pos != std::string::npos && pos > 0)
                    Shell::s_cwd.resize(pos);
                else
                    Shell::s_cwd = "/";
            }
            continue;
        }
        if (parts[i] == ".") continue;

        unsigned int idx2 = dirs.namei(parts[i].c_str());
        if (idx2 == (unsigned int)-1) {
            dirs.mkdir(parts[i].c_str(), 0);
        } else {
            // Entry exists — verify it's a directory
            inode* t = g_core.icache().iget(dirs.current_dir().entries[idx2].d_ino);
            bool ok = t && (t->di_mode & DIDIR);
            if (t) g_core.icache().iput(t);
            if (!ok) {
                std::cout << "  '" << parts[i] << "' is not a directory.\n";
                unwind_path(depth);
                return {"", -1};
            }
        }
        dirs.chdir(parts[i].c_str());
        if (Shell::s_cwd.back() != '/') Shell::s_cwd += '/';
        Shell::s_cwd += parts[i];
        depth++;
    }

    return {parts.back(), depth};
}

static void unwind_path(int depth)
{
    for (int i = 0; i < depth; i++) {
        g_core.dirs().chdir("..");
        auto pos = Shell::s_cwd.rfind('/');
        if (pos != std::string::npos && pos > 0)
            Shell::s_cwd.resize(pos);
        else
            Shell::s_cwd = "/";
    }
}

/** Navigate into path without creating directories. Returns {final_name, depth}. */
static std::pair<std::string, int> navigate_path(const std::string& path)
{
    auto& dirs = g_core.dirs();
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string token;
    while (std::getline(iss, token, '/'))
        parts.push_back(token);

    if (parts.empty()) return {"", 0};

    int depth = 0;
    // Process ALL components including the last
    for (size_t i = 0; i < parts.size(); i++) {
        if (parts[i] == "..") {
            if (dirs.namei("..") != (unsigned int)-1) {
                dirs.chdir("..");
                if (depth > 0) depth--;
                auto pos = Shell::s_cwd.rfind('/');
                Shell::s_cwd = (pos != std::string::npos && pos > 0) ? Shell::s_cwd.substr(0, pos) : "/";
            }
            continue;
        }
        if (parts[i] == ".") continue;

        unsigned int idx = dirs.namei(parts[i].c_str());
        if (idx == (unsigned int)-1) {
            unwind_path(depth);
            return {parts[i], -1};
        }
        // Check it's actually a directory before entering
        inode* target = g_core.icache().iget(dirs.current_dir().entries[idx].d_ino);
        bool is_dir = target && (target->di_mode & DIDIR);
        if (target) g_core.icache().iput(target);
        if (!is_dir) {
            unwind_path(depth);
            return {parts[i], -1};
        }
        dirs.chdir(parts[i].c_str());
        if (Shell::s_cwd.back() != '/') Shell::s_cwd += '/';
        Shell::s_cwd += parts[i];
        depth++;
    }
    return {parts.back(), depth};   // parts.back() for use as display name
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
    std::cout << "Commands:\n"
              << "  login  <uid> <passwd>       Log in\n"
              << "  logout                       Log out\n"
              << "  ls | dir                     List current directory\n"
              << "  mkdir <name>                 Create subdirectory\n"
              << "  cd    <path>                 Change directory\n"
              << "  creat <path> [mode]          Create file  (default mode 0777)\n"
              << "  read  <path> [size]          Read file   (default: all bytes)\n"
              << "  write [-o] <path> [text]     Append / overwrite file\n"
              << "  delete <path>                Delete file\n"
              << "  find  <name>                 Recursively search for name\n"
              << "  halt | exit | quit           Shutdown\n";

    if (g_core.users().current_user().u_uid == 0) {
        std::cout << "\n  --- admin (uid=0) ---\n"
                  << "  reg  <uid> <gid> <passwd>   Register new user\n"
                  << "  uls                          List all users\n";
    }
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
    auto& dirs   = g_core.dirs();
    auto& files  = g_core.files();
    auto& users  = g_core.users();
    auto& icache = g_core.icache();

    std::string line;
    while (true) {
        show_prompt();
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
        //  reg  <uid>  <gid>  <password>   (uid=0 only)
        // ============================================================
        else if (cmd == "reg") {
            if (users.current_user().u_uid != 0) {
                std::cout << "  reg: root (uid=0) only.\n";
                continue;
            }
            if (args.size() < 4) {
                std::cout << "Usage: reg <uid> <gid> <password>\n";
                continue;
            }
            unsigned long new_uid = 0, new_gid = 0;
            if (!parse_uint(args[1], new_uid) || !parse_uint(args[2], new_gid))
                continue;

            // Find empty slot in password table
            pwd* tbl = users.pwd_table();
            int slot = -1;
            for (int i = 0; i < PWDNUM; i++) {
                if (tbl[i].p_uid == 0 && tbl[i].p_gid == 0 && tbl[i].password[0] == '\0') {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                std::cout << "  Password table full.\n";
                continue;
            }
            tbl[slot].p_uid = static_cast<uint16_t>(new_uid);
            tbl[slot].p_gid = static_cast<uint16_t>(new_gid);
            strncpy(tbl[slot].password, args[3].c_str(), PWDSIZ - 1);
            tbl[slot].password[PWDSIZ - 1] = '\0';
            users.save_password_file();
            std::cout << "  Registered user uid=" << new_uid << " gid=" << new_gid << ".\n";
        }

        // ============================================================
        //  uls   (uid=0 only — list all users)
        // ============================================================
        else if (cmd == "uls") {
            if (users.current_user().u_uid != 0) {
                std::cout << "  uls: root (uid=0) only.\n";
                continue;
            }
            pwd* tbl = users.pwd_table();
            std::cout << "  uid  gid  password\n";
            std::cout << "  ---  ---  --------\n";
            for (int i = 0; i < PWDNUM; i++) {
                if (tbl[i].password[0] != '\0')
                    std::cout << "  " << tbl[i].p_uid << "    "
                              << tbl[i].p_gid << "    "
                              << tbl[i].password << "\n";
            }
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
            auto [fname, depth] = resolve_path(args[1]);
            dirs.mkdir(fname.c_str(), users.current_user_id());
            unwind_path(depth);
        }

        // ============================================================
        //  cd  <name>
        // ============================================================
        else if (cmd == "cd") {
            if (args.size() < 2) {
                std::cout << "Usage: cd <path | ~>\n";
                continue;
            }
            if (args[1] == "~") {
                // Go to root: keep going up until inode stops changing
                while (true) {
                    auto prev = dirs.cur_path_inode() ? dirs.cur_path_inode()->i_ino : 0;
                    dirs.chdir("..");
                    auto cur = dirs.cur_path_inode() ? dirs.cur_path_inode()->i_ino : 0;
                    if (cur == prev) break;   // root's ".." = root itself
                }
                Shell::s_cwd = "/";
                continue;
            }
            auto [bad, ok] = navigate_path(args[1]);
            if (ok < 0)
                std::cout << "  '" << bad << "' not found or not a directory.\n";
        }


        else if (cmd == "creat") {
            if (args.size() < 2) {
                std::cout << "Usage: creat <filename> [mode]\n";
                continue;
            }
            unsigned long mode = DEFAULTMODE;
            if (args.size() >= 3 && !parse_uint(args[2], mode)) continue;

            auto [fname, depth] = resolve_path(args[1]);

            // Check if file already exists in this directory
            if (dirs.namei(fname.c_str()) != (unsigned int)-1) {
                std::cout << "  '" << fname << "' already exists.\n";
                unwind_path(depth);
                continue;
            }

            int fd = files.creat(users.current_user(), fname.c_str(),
                                 static_cast<uint16_t>(mode));
            if (fd >= 0) {
                bind_fd(args[1], fd);
                std::cout << "  Created '" << args[1] << "'  fd=" << fd
                          << "  mode=" << std::oct << mode << std::dec << "\n";
            } else {
                std::cout << "  creat failed.\n";
            }
            unwind_path(depth);
        }

        // ============================================================
        //  read  <name>  [size]
        // ============================================================
        else if (cmd == "read") {
            if (args.size() < 2) {
                std::cout << "Usage: read <filename> [size]\n";
                continue;
            }
            const std::string orig_path = args[1];
            auto [fname, depth] = resolve_path(orig_path);

            int fd = lookup_fd(orig_path);
            if (fd < 0) {
                uint16_t new_fd = files.open(users.current_user(), fname.c_str(), FREAD);
                if (new_fd == static_cast<uint16_t>(-1)) {
                    std::cout << "  Cannot open '" << orig_path << "' for reading.\n";
                    unwind_path(depth);
                    continue;
                }
                fd = new_fd;
                bind_fd(orig_path, fd);
            }

            // Reject directories
            if (files.ofile_table()[users.current_user().u_ofile[fd]].f_inode->di_mode & DIDIR) {
                std::cout << "  '" << orig_path << "' is a directory.\n";
                files.close(users.current_user(), static_cast<uint16_t>(fd));
                unbind_fd(orig_path);
                unwind_path(depth);
                continue;
            }

            uint32_t max_size = file_size(fd);
            if (args.size() >= 3) {
                unsigned long s = 0;
                if (!parse_uint(args[2], s)) {
                    files.close(users.current_user(), static_cast<uint16_t>(fd));
                    unbind_fd(orig_path);
                    unwind_path(depth);
                    continue;
                }
                max_size = static_cast<uint32_t>(s);
            }

            if (max_size == 0) {
                std::cout << "  (empty file)\n";
                files.close(users.current_user(), static_cast<uint16_t>(fd));
                unbind_fd(orig_path);
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
            unbind_fd(orig_path);
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

            const std::string orig_path = args[name_idx];
            auto [fname, depth] = resolve_path(orig_path);
            int fd = lookup_fd(orig_path);

            if (overwrite) {
                // Overwrite mode: close if open, creat (truncates), rebind
                if (fd >= 0) {
                    files.close(users.current_user(), static_cast<uint16_t>(fd));
                    unbind_fd(orig_path);
                }
                int cfd = files.creat(users.current_user(), fname.c_str(), DEFAULTMODE);
                if (cfd < 0) {
                    std::cout << "  Cannot overwrite '" << fname << "'.\n";
                    continue;
                }
                fd = cfd;
                bind_fd(orig_path, fd);
                std::cout << "  (overwrite mode, fd=" << fd << ")\n";
            } else {
                // Append mode: always open fresh with FAPPEND
                if (fd >= 0) {
                    files.close(users.current_user(), static_cast<uint16_t>(fd));
                    unbind_fd(orig_path);
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
                bind_fd(orig_path, fd);
            }

            // Reject directories
            {
                auto& u = users.current_user();
                inode* ino = files.ofile_table()[u.u_ofile[fd]].f_inode;
                if (ino && (ino->di_mode & DIDIR)) {
                    std::cout << "  '" << orig_path << "' is a directory.\n";
                    files.close(users.current_user(), static_cast<uint16_t>(fd));
                    unbind_fd(orig_path);
                    unwind_path(depth);
                    continue;
                }
            }

            // Gather text
            std::string text;
            size_t text_start = name_idx + 1;
            bool use_editor = (args.size() <= text_start);

            if (use_editor) {
                text = editor_read();     // always blank — mode handles position
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
            unbind_fd(orig_path);
        }

        // ============================================================
        //  delete  <name>
        // ============================================================
        else if (cmd == "delete") {
            if (args.size() < 2) {
                std::cout << "Usage: delete <filename>\n";
                continue;
            }
            const std::string orig_path = args[1];
            auto [fname, depth] = resolve_path(orig_path);

            int fd = lookup_fd(orig_path);
            if (fd >= 0) {
                files.close(users.current_user(), static_cast<uint16_t>(fd));
                unbind_fd(orig_path);
            }
            files.delete_file(fname.c_str());
            std::cout << "  Deleted '" << orig_path << "'.\n";
            unwind_path(depth);
        }

        // ============================================================
        //  find  <name>
        // ============================================================
        else if (cmd == "find") {
            if (args.size() < 2) {
                std::cout << "Usage: find <name>\n";
                continue;
            }

            // Recursive search helper
            struct {
                void search(DirectoryManager& dirs, InodeCache& icache,
                           const std::string& prefix, const std::string& name) const
                {
                    auto& entries = dirs.current_dir().entries;
                    std::vector<uint32_t> subdirs;

                    for (size_t i = 0; i < entries.size(); i++) {
                        if (entries[i].d_ino == 0) continue;
                        if (strcmp(entries[i].d_name, ".") == 0 ||
                            strcmp(entries[i].d_name, "..") == 0) continue;

                        inode* f = icache.iget(entries[i].d_ino);
                        bool is_file = f && !(f->di_mode & DIDIR);
                        if (f) icache.iput(f);
                        if (strcmp(entries[i].d_name, name.c_str()) == 0 && is_file)
                            std::cout << "  " << prefix << "/" << name << "\n";

                        // Collect subdirectories for recursion
                        inode* ino = icache.iget(entries[i].d_ino);
                        if (ino && (ino->di_mode & DIDIR))
                            subdirs.push_back(entries[i].d_ino);
                        if (ino) icache.iput(ino);
                    }

                    for (uint32_t ino_num : subdirs) {
                        inode* sub = icache.iget(ino_num);
                        if (!sub) continue;
                        // Find the name of this subdirectory
                        std::string subname;
                        for (auto& e : entries)
                            if (e.d_ino == ino_num) { subname = e.d_name; break; }

                        dirs.chdir(subname.c_str());
                        search(dirs, icache, prefix + "/" + subname, name);
                        dirs.chdir("..");
                        icache.iput(sub);
                    }
                }
            } recursive_find;

            auto saved_ino = dirs.cur_path_inode() ? dirs.cur_path_inode()->i_ino : 0;
            recursive_find.search(dirs, icache, s_cwd == "/" ? "" : s_cwd, args[1]);
            // Restore original directory
            while (dirs.cur_path_inode() && dirs.cur_path_inode()->i_ino != saved_ino) {
                dirs.chdir("..");
            }
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
