// ── test_fixy_profile — sentinel for fixy/Profile.h (FIXY-AUDIT-B2) ─
//
// Pulls fixy/Profile.h into a TU compiled under project warning
// flags so the header's static_asserts execute.  Three claims:
//
//   1. `fixy_is_strict` reflects the CMake toggle's current value.
//      Under default preset CRUCIBLE_FIXY_STRICT=1, so this evaluates
//      to true at compile time.
//   2. `IsAcceptedSketch` is the always-true concept — accepts ANY
//      arguments including empty Grants pack, void Type, etc.
//   3. `IsAcceptedActive<Type, Grants...>` aliases to the strict or
//      permissive concept based on the toggle.  Under STRICT, an
//      all-strict pack accepts; an empty Grants pack rejects.  Under
//      SKETCH, both accept.

#include <crucible/fixy/Profile.h>

#include <type_traits>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// ─── 1. fixy_is_strict matches the CMake toggle ───────────────────

#if CRUCIBLE_FIXY_STRICT
static_assert(fixy::fixy_is_strict,
    "Under CRUCIBLE_FIXY_STRICT=1, fixy_is_strict must be true.");
#else
static_assert(!fixy::fixy_is_strict,
    "Under CRUCIBLE_FIXY_STRICT=0, fixy_is_strict must be false.");
#endif

// ─── 2. IsAcceptedSketch is permissive ────────────────────────────

static_assert(fixy::IsAcceptedSketch<int>,
    "IsAcceptedSketch<int> must accept the empty Grants pack.");
static_assert(fixy::IsAcceptedSketch<void>,
    "IsAcceptedSketch<void> must accept — sketch mode is permissive.");
static_assert(fixy::IsAcceptedSketch<int, strict<D::Usage>>,
    "IsAcceptedSketch<int, partial pack> must accept.");

// ─── 3. IsAcceptedActive routes per the toggle ────────────────────

// Under STRICT mode: an empty Grants pack rejects (no engagements).
// Under SKETCH mode: empty pack accepts (always-true).
#if CRUCIBLE_FIXY_STRICT
static_assert(!fixy::IsAcceptedActive<int>,
    "Under STRICT, IsAcceptedActive<int> with empty pack must reject "
    "(every dim must be engaged).");
#else
static_assert(fixy::IsAcceptedActive<int>,
    "Under SKETCH, IsAcceptedActive<int> with empty pack must accept "
    "(always-true).");
#endif

// All-strict pack accepts under both modes.
static_assert(fixy::IsAcceptedActive<int,
    strict<D::Type>, strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>>,
    "An all-strict 20-axis pack must accept under both modes.");

int main() { return 0; }
