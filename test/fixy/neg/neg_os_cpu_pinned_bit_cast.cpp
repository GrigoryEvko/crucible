// The fifth route to a pin proof.  A deleted copy with a defaulted move
// would leave CpuPinned trivially copyable, and std::bit_cast would then
// build a pin with no call to sched_setaffinity.  The move constructor is
// user-provided, so the class is not trivially copyable and bit_cast
// refuses it at its constraint.  See neg_os_cpu_pinned_value_ctor.cpp for why a forged pin
// matters.  The source has the size of the pin, so the refusal is the
// copy rule and not the size rule.

#include <fixy/os/CpuPinned.h>

#include <array>
#include <bit>

namespace ml = foundation::algebra::lattices;

int main() {
    using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;
    constexpr std::array<unsigned char, sizeof(Pin)> bytes{};
    auto forged = std::bit_cast<Pin>(bytes);
    (void)forged;
    return 0;
}
