// A member type is a public member too, so a member type with a name of no
// list fails the build.

#include <foundation/core/Verbs.h>

template <class T>
struct Probe {
    using value_type = T;
};

CRUCIBLE_CLOSED_SURFACE(::foundation::core::TextView{"is_some"}, Probe<int>);

int main() { return 0; }
