// A region that the two macros open and close turns each finding inside it
// into opted_out.  A finding before or after the region keeps its kind.
#include <foundation/Quarantine.h>
#include <cstring>

namespace probe {

void before_region(char* bytes) { std::memset(bytes, 0, 1); }

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test needs a raw buffer")
void inside_region(char* bytes) { std::memset(bytes, 0, 1); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

void after_region(char* bytes) { std::memset(bytes, 0, 1); }

}  // namespace probe
