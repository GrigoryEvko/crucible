// A region that a macro closes.  With the plugin of main, the region ended at
// the line of the definition of the macro, before the begin of the region,
// and the compile gave no error for that end.  The plugin refuses the use
// inside another macro.

#include <foundation/Quarantine.h>

#define WRAPPER_CLOSE CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a region that a macro closes")
inline int* inside_pointer = nullptr;
WRAPPER_CLOSE
