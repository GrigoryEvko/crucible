// A region that a macro of the quarantined file opens.  The ledger of the
// regions reads the body of a macro definition as one text token, so it sees
// no region here.  With the plugin of main, the region started at the line of
// the definition, so the pointer before the use of the macro was opted out.
// The plugin refuses the use inside another macro.

#include <foundation/Quarantine.h>

#define WRAPPER_OPEN CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a region that a macro opens")
inline int* before_use_pointer = nullptr;
WRAPPER_OPEN
inline int* wrapper_pointer = nullptr;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
