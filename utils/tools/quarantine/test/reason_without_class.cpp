// A region whose reason does not start with its class is an error.
#include <foundation/Quarantine.h>
CRUCIBLE_I_KNOW_WHAT_IM_DOING("a reason with no class")
int classless_value = 0;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
