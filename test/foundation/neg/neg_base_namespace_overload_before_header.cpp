// The rule base-namespace of the quarantine plugin refuses a new overload of
// a function of the base that a file outside the base declares in the
// namespace of the base before the header.  A qualified call of the base binds
// each overload that the unit declares before the call, so the overload
// changes which function the call reaches.  The plugin makes the declaration
// a compile error in each build.

namespace foundation::core {
constexpr bool unreachable(int code) noexcept { return code == 0; }
}  // namespace foundation::core

#include <foundation/core/Report.h>

int main() { return foundation::core::unreachable(0) ? 0 : 1; }
