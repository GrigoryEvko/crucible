// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: applying operator<=> across HappensBeforeLattice<N, Tag>
// instantiations with different Tag (same N).
//
// Symmetric to neg_happens_before_cross_tag_mixing.cpp but pinning the
// Tag distinction at the C++20 SPACESHIP surface, not just the static
// `leq` member.  Without this test, a refactor that left static `leq`
// strict on Tag but accidentally widened the spaceship to be templated
// over (TagA, TagB) would silently allow `replay_clock < kernel_clock`
// — defeating the protocol-distinction guarantee at the most idiomatic
// call site users reach for.
//
// Both Tag-distinction surfaces (member leq AND member <=>) must
// remain Tag-strict.  The cross-N spaceship test catches one form of
// spaceship-leniency drift; this catches the other.
//
// [GCC-WRAPPER-TEXT] — operator-resolution rejection on cross-Tag
// spaceship.

#include <crucible/algebra/lattices/HappensBefore.h>

#include <compare>

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

    // Should FAIL: operator<=> is a member of element_type taking
    // `element_type const&` — bound to THIS specific (N, Tag)
    // instantiation.  Cross-Tag spaceship invocation rejected at
    // overload resolution.
    [[maybe_unused]] auto ord = replay_clock <=> kernel_clock;
    return 0;
}
