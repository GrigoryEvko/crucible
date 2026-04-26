// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a HappensBeforeLattice<N, TagA>::element_type
// to a function expecting HappensBeforeLattice<N, TagB>::element_type
// for TagA ≠ TagB (same N, different protocol Tag).
//
// The Tag template parameter is the WHOLE POINT of HappensBeforeLattice's
// optional second parameter — it gives cross-protocol distinction so
// (e.g.) a ReplayClock vector and a KernelOrderClock vector cannot be
// silently mixed even when both are 4-participant clocks with the same
// underlying storage shape.
//
// Pins the protocol-distinction contract: a future refactor that
// elided the Tag template parameter or made it implicitly convertible
// across protocols would silently allow a ReplayClock to flow into
// a KernelOrderClock lattice op — defeating the whole point of the
// per-protocol Tag.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection.  Same rationale
// as cross-N: structurally-distinct types per Tag, no framework
// static_assert needed.

#include <crucible/algebra/lattices/HappensBefore.h>

using namespace crucible::algebra::lattices;

namespace {
struct ReplayClockTag {};
struct KernelOrderClockTag {};
}  // namespace

int main() {
    using HBReplay = HappensBeforeLattice<4, ReplayClockTag>;
    using HBKernel = HappensBeforeLattice<4, KernelOrderClockTag>;

    HBReplay::element_type replay_clock{{1, 0, 0, 0}};
    HBKernel::element_type kernel_clock{{0, 1, 0, 0}};

    // Should FAIL: HBReplay::leq expects two HBReplay::element_type
    // arguments; kernel_clock is HBKernel::element_type — different
    // template instantiation, different type, no implicit conversion
    // (the whole point of the Tag).
    return static_cast<int>(HBReplay::leq(replay_clock, kernel_clock));
}
