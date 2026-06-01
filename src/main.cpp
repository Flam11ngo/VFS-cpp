/**
 * main.cpp — VFS entry point
 */
#include "Core.h"

int main()
{
    g_core.init();
    Shell::run();
    g_core.exit();
}
