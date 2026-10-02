// A region that no END macro closes is an error.
#include <foundation/Quarantine.h>
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a region that nothing closes")
int unclosed_value = 0;
