// ── test_fixy_profile — sentinel for fixy/Profile.h (FIXY-AUDIT-B2) ─
//
// Pulls fixy/Profile.h into a TU compiled under project warning
// flags so the header's static_asserts execute.  Three claims:
//
//   1. `fixy_is_strict` reflects the CMake toggle's current value.
//      Under default preset CRUCIBLE_FIXY_STRICT=1, so this evaluates
//      to true at compile time.
//   2. `IsAcceptedSketch` is permissive on the Grants axis but strict
//      on the Type axis — accepts any Grants-pack shape so long as
//      the Type payload is structurally valid (fixy-A4-031).  Void,
//      cv-qualified, reference, array, and bare function-type
//      payloads reject under BOTH STRICT and SKETCH modes.
//   3. `IsAcceptedActive<Type, Grants...>` aliases to the strict or
//      permissive concept based on the toggle.  Under STRICT, an
//      all-strict pack accepts; an empty Grants pack rejects.  Under
//      SKETCH, both accept (modulo Type-axis well-formedness).

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

// ─── 2. IsAcceptedSketch — Grants-permissive, Type-strict (A4-031) ─

// Type-axis ACCEPT witnesses.  Scalars, object pointers, and
// function pointers are accepted payloads regardless of pack content.
static_assert(fixy::IsAcceptedSketch<int>,
    "IsAcceptedSketch<int> must accept the empty Grants pack.");
static_assert(fixy::IsAcceptedSketch<int*>,
    "IsAcceptedSketch<int*> must accept — object pointers.");
static_assert(fixy::IsAcceptedSketch<int(*)(int)>,
    "IsAcceptedSketch<int(*)(int)> must accept — function POINTERS "
    "are object types.");
static_assert(fixy::IsAcceptedSketch<int, strict<D::Usage>>,
    "IsAcceptedSketch<int, partial pack> must accept — Grants axis "
    "is permissive.");

// Type-axis REJECT witnesses — fixy-A4-031.  Sketch mode preserves
// Type-axis structural validity; void / cv-qualified / reference /
// array / bare function-type payloads all reject at the requires-
// clause instead of producing a deep substrate-internal diagnostic.
static_assert(!fixy::IsAcceptedSketch<void>,
    "fixy-A4-031: IsAcceptedSketch<void> must reject — sketch mode "
    "does not bypass the Type-axis floor.");
static_assert(!fixy::IsAcceptedSketch<const int>,
    "fixy-A4-031: top-level const Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<volatile int>,
    "fixy-A4-031: top-level volatile Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int&>,
    "fixy-A4-031: lvalue-reference Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int&&>,
    "fixy-A4-031: rvalue-reference Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int[5]>,
    "fixy-A4-031: array Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int(int)>,
    "fixy-A4-031: bare function-type Type must reject under SKETCH.");

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
// IsAcceptedActive (= IsAccepted under STRICT) auto-injects the Type
// marker so the user pack must NOT include strict<D::Type> explicitly.
static_assert(fixy::IsAcceptedActive<int,
    strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>,
    "An all-strict 21-axis pack must accept under both modes.");

int main() { return 0; }
