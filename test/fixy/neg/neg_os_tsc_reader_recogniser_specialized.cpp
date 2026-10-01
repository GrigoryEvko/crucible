// The recogniser of a pin cannot be specialized.  A variable template
// in its place would let a translation unit declare a fake class a pin,
// and the fake would then pass IsSingletonCpuPin with no pin behind it.
// The recogniser is the reflection query of
// foundation/reflect/Instance.h, a concept.  No such variable template
// exists, so this specialization names nothing.
//
// The static_assert names the recogniser of the header, and it shows that
// the fake is no pin.  Without the header it is a second error, and the test
// runs with CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1, so the fixture passes only
// when the specialization is its one error.

#include <fixy/os/Time.h>

struct Forged {
    static constexpr bool is_singleton_pin = true;
};

namespace fixy::time::detail {

template <>
inline constexpr bool is_cpu_pinned_v<::Forged> = true;

}  // namespace fixy::time::detail

static_assert(!::fixy::time::IsSingletonCpuPin<Forged>);

int main() { return 0; }
