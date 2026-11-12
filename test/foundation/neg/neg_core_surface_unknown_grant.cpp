// A check grants only a language protocol that the language has, so a grant
// of a different type is refused.

#include <foundation/core/Verbs.h>

struct Probe {
    constexpr int operator()() const noexcept { return 0; }
};

struct CallProtocol {};

inline constexpr ::foundation::core::TextView probe_names{"is_some"};

static_assert(::foundation::core::closed_surface<Probe, CallProtocol>(probe_names).is_closed());

int main() { return 0; }
