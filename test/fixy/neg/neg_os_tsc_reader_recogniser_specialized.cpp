// The recogniser of a pin cannot be specialized.  A variable template
// in its place would let a translation unit declare a fake class a pin,
// and the fake would then pass IsSingletonCpuPin with no pin behind it.
// The recogniser is the reflection query of
// foundation/reflect/Instance.h, a concept.  No such variable template
// exists, so this specialization names nothing.

#include <fixy/os/Time.h>

struct Forged {
    static constexpr bool is_singleton_pin = true;
};

namespace fixy::time::detail {

template <>
inline constexpr bool is_cpu_pinned_v<::Forged> = true;

}  // namespace fixy::time::detail

int main() { return 0; }
