// A quarantined unit that includes gapsother/sys/Marked.h through -isystem.
// check_plugin.py compiles it with the directory gapsother/sys as a system
// include directory.

#include <Marked.h>

int marked_use() { return marked::marked_count(); }
