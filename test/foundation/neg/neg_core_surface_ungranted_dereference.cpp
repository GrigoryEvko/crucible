// A type that can be empty has no dereference, so a check that grants no
// protocol refuses operator*.

#include <foundation/core/Verbs.h>

struct Probe {
    constexpr int operator*() const noexcept { return 0; }
};

CRUCIBLE_CLOSED_SURFACE(::foundation::core::TextView{"is_some"}, Probe);

int main() { return 0; }
