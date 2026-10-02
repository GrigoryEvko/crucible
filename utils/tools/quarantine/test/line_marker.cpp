// A line marker enters a file that does not exist, with the flag of a system
// header.  The pointer in it gives no finding with the plugin of main.  The
// plugin refuses a line marker that enters a file, because only an #include
// directive enters a file of the tree.

# 1 "fake_system.h" 1 3
inline int* inside_marker = nullptr;
# 9 "" 2
