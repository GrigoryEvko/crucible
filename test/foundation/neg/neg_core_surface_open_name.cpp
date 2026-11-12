// A family type gives only the shared verbs, the language protocols and the
// names of its family list.  A member with a name of no list fails the
// build, and the message names the member.

#include <foundation/core/Verbs.h>

struct Probe {
    [[nodiscard]] constexpr bool has_value() const noexcept { return false; }
};

CRUCIBLE_CLOSED_SURFACE(::foundation::core::TextView{"is_some"}, Probe);

int main() { return 0; }
