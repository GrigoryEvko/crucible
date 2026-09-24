// A deleted copy with a defaulted move left CpuPinned trivially copyable,
// so std::bit_cast built a pin proof from an int with no syscall.  The move
// constructor is user-provided now, so bit_cast refuses the class at its
// constraint.

#include <crucible/safety/_CpuPinned.h>

#include <crucible/algebra/lattices/_AffinityLattice.h>

#include <bit>

using AffinityMask = ::crucible::algebra::lattices::AffinityMask;

int main() {
    using Pin =
        ::crucible::safety::CpuPinned<AffinityMask::single(0), ::crucible::safety::PinningPosture::PinnedExplicit, int>;
    auto forged = std::bit_cast<Pin>(0);
    return forged.peek();
}
