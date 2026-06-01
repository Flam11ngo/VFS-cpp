#pragma once

/**
 * Shell — Layer 7: Command REPL
 *
 * Parses user input and dispatches to the VFS subsystems via g_core.
 */
class Shell {
public:
    /** Enter the command loop.  Reads from stdin until EOF or halt/exit/quit. */
    static void run();
};
