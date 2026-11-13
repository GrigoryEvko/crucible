// The rule base-namespace of the quarantine plugin refuses a using-directive
// of the global namespace before a header of the base.  An unqualified lookup
// of the base that reaches the global namespace then reads each name that the
// directive nominates.  The plugin makes the directive a compile error in each
// build.

namespace planted {
constexpr int pick(long) { return 1; }
}  // namespace planted

using namespace planted;

#include <foundation/core/Report.h>

int main() { return pick(1L) == 1 ? 0 : 1; }
