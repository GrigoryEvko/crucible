// A region whose reason comes through a macro.  The ledger of the regions
// reads a string literal in the call, so it sees no region here, and the
// plugin of main opened one.  The plugin refuses the call.

#include <foundation/Quarantine.h>

#define MACRO_REASON "PROBE: a reason through a macro"
CRUCIBLE_I_KNOW_WHAT_IM_DOING(MACRO_REASON)
inline int* reason_pointer = nullptr;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
