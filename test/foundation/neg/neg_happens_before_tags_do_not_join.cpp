// Two clocks of one width and different tags belong to two protocols.
// The join of one takes its own element type only, so a replay clock
// cannot absorb a kernel-order clock.

#include <foundation/algebra/lattices/HappensBefore.h>

struct ReplayClock {};
struct KernelOrderClock {};

int main() {
    namespace fl = ::foundation::algebra::lattices;
    using Replay = fl::HappensBeforeLattice<2, ReplayClock>;
    using KernelOrder = fl::HappensBeforeLattice<2, KernelOrderClock>;
    Replay::element_type const replay{{1, 0}};
    KernelOrder::element_type const kernel{{0, 1}};
    return static_cast<int>(Replay::join(replay, kernel)[0]);
}
