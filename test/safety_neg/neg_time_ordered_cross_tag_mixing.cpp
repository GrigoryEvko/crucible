// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a TimeOrdered<T, N, TagA> to a method expecting
// TimeOrdered<T, N, TagB> for TagA ≠ TagB (same N, different
// protocol Tag).
//
// Symmetric to neg_happens_before_cross_tag_mixing but at the
// WRAPPER surface — pins that TimeOrdered preserves the per-Tag
// distinction the lattice already enforces.  The Tag template
// parameter exists specifically to prevent ReplayClock events from
// silently flowing into KernelOrderClock contexts; this neg-compile
// pins that the wrapper-level Tag binding can't be bypassed via
// implicit conversion.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection.

#include <crucible/safety/TimeOrdered.h>

using namespace crucible::safety;

namespace {
struct ReplayClockTag {};
struct KernelOrderClockTag {};
}  // namespace

int main() {
    using TO_Replay = TimeOrdered<int, 4, ReplayClockTag>;
    using TO_Kernel = TimeOrdered<int, 4, KernelOrderClockTag>;

    TO_Replay replay_evt{};
    TO_Kernel kernel_evt{};

    // Should FAIL: TO_Replay::happens_before takes TO_Replay const&;
    // kernel_evt is TO_Kernel — different specialization (per-Tag),
    // no implicit conversion.
    return static_cast<int>(replay_evt.happens_before(kernel_evt));
}
