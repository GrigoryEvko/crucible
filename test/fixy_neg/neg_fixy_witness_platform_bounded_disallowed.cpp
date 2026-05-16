// ── neg_fixy_witness_platform_bounded_disallowed (FIXY-G9 HS14) ──────
//
// Pins PlatformBounded behavior: a Tested witness bounded to a
// non-current arch DEGRADES to the Asserted floor in the lattice.
// On x86-64 build, `PlatformBounded<Tested<id>, AArch64>` has
// witness_tier_v = 1 (Asserted), not 2 (Tested).
//
// The assertion below INTENTIONALLY claims the bounded witness still
// satisfies a Tested floor; when the discipline is intact, the
// assertion fails with the embedded "WitnessAtLeast" / "PlatformBounded"
// rationale.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Witness.h>
#include <crucible/safety/witness/Platform.h>
#include <crucible/safety/witness/Witness.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace sw = crucible::safety::witness;

namespace {

// Pick an arch that's NOT current.  Mirrors the in-header
// pb_inactive_arch trick.
#if defined(__x86_64__)
using OtherArch = sw::arch::AArch64;
#elif defined(__aarch64__)
using OtherArch = sw::arch::X86_64;
#else
using OtherArch = sw::arch::X86_64;
#endif

using BoundedTested = sw::PlatformBounded<sw::Tested<7>, OtherArch>;

using BoundedFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cg::reentrant_e<BoundedTested>,                  // PlatformBounded witness
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// THE DISCIPLINE BEING PINNED: PlatformBounded<Tested<id>, OtherArch>
// degrades to Asserted on the current arch.  The assertion is the
// INVERSE — claims the bounded witness still satisfies Tested floor.
// Build red is the expected outcome; the canonical phrase appears in
// the failure.
static_assert(cf::FnWitnessAtLeast<BoundedFn, cd::Reentrancy, sw::Tested<0>>,
    "PlatformBounded fixture: a Tested witness bounded to a non-current "
    "arch degrades to the Asserted floor; FnWitnessAtLeast against the "
    "Tested floor must FAIL.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
