// A public base adds its members to the surface of a family type, so a
// member of the base with a name of no list fails the build too.

#include <foundation/core/Verbs.h>

struct Base {
    int raw = 0;
};

struct Probe : Base {
    [[nodiscard]] constexpr int size() const noexcept { return raw; }
};

CRUCIBLE_CLOSED_SURFACE(::foundation::core::TextView{"is_some"}, Probe);

int main() { return 0; }
