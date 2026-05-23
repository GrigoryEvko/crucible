// FIXY-V-184 sentinel TU: algebra/lattices/ClockSourceLattice.h —
// COMPOSITE product lattice over DetSafe × SuspendBehavior × Pinning,
// plus the 9-source `ClockSource` value vocabulary and its
// many-to-one projection onto the product's 3-tuple element.
//
// V-184 ships the lattice + enum + projection (the algebraic substrate
// + value-level FIXING function).  It ships NO row_hash and NO
// DimensionAxis enumerator:
//   - FIXY-V-185 ships safety/ClockSource.h (the Graded carrier keyed
//     on the `ClockSource` NTTP) PLUS the
//     row_hash_contribution<ClockSource<...>> federation-cache
//     discriminator — exactly mirroring MemoryScopeLattice (V-265) →
//     safety/ScopedFence.h (V-267).  The lattice layer pulls NO
//     safety/diag header.  A COMPOSITE additionally carries no salt of
//     its own (FpModeProductLattice precedent): the federation
//     contribution composes through the per-axis component WRAPPERS.
//
// THE LOAD-BEARING PROPERTIES this TU defends:
//   (1) Every ClockSource projects to its documented (DetSafe,
//       Suspend, Pin) tuple — the three task-FIXED rows (Realtime,
//       Boot, TscRaw) plus the six derived rows.
//   (2) The order is a genuine PRODUCT (pointwise AND), not a chain:
//       there exist INCOMPARABLE points, and Boot ⊏ TscRaw is a strict
//       order via the pinning axis alone.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ClockSourceLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;

namespace {

using cal::ClockSource;
using cal::DetSafeTier;
using cal::PinningRequirement;
using cal::SuspendBehavior;
using L = cal::ClockSourceLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>,
    "FIXY-V-184: ClockSourceLattice must satisfy the Lattice concept.");
static_assert(crucible::algebra::BoundedLattice<L>,
    "FIXY-V-184: the product of three bounded chains is a bounded lattice.");
static_assert(crucible::algebra::BoundedBelowLattice<L>);
static_assert(crucible::algebra::BoundedAboveLattice<L>);
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>);

// ── Cardinality — nine value-level sources ──────────────────────────
static_assert(cal::clock_source_count == 9,
    "FIXY-V-184: ClockSource must have exactly 9 enumerators; FIXY-V-201 "
    "appends PtpHwClock at the next free ordinal (append-only).");
static_assert(std::is_same_v<std::underlying_type_t<ClockSource>, std::uint8_t>,
    "FIXY-V-184: ClockSource uses uint8_t underlying; the ordinal is "
    "declaration order, NOT an order-semantics rank.");

// ── Arity + named axis projections ──────────────────────────────────
static_assert(L::arity == 3);
static_assert(std::is_same_v<L::nth_lattice<0>, cal::DetSafeLattice>);
static_assert(std::is_same_v<L::nth_lattice<1>, cal::SuspendBehaviorLattice>);
static_assert(std::is_same_v<L::nth_lattice<2>, cal::PinningRequirementLattice>);
static_assert(std::is_same_v<L::det_safe_axis, cal::DetSafeLattice>);
static_assert(std::is_same_v<L::suspend_axis,  cal::SuspendBehaviorLattice>);
static_assert(std::is_same_v<L::pinning_axis,  cal::PinningRequirementLattice>);

// ── Bounds — pointwise lifts of the component endpoints ─────────────
static_assert(L::get<0>(L::bottom()) == DetSafeTier::NonDeterministicSyscall);
static_assert(L::get<1>(L::bottom()) == SuspendBehavior::Unknown);
static_assert(L::get<2>(L::bottom()) == PinningRequirement::NotRequired);
static_assert(L::get<0>(L::top())    == DetSafeTier::Pure);
static_assert(L::get<1>(L::top())    == SuspendBehavior::KeepsTicking);
static_assert(L::get<2>(L::top())    == PinningRequirement::CrossSocketSafe);

// ── Projection matrix — the three task-FIXED rows are load-bearing ──
[[nodiscard]] consteval bool projects_to(
    ClockSource source, DetSafeTier det, SuspendBehavior suspend,
    PinningRequirement pin) noexcept
{
    auto point = cal::clock_source_project(source);
    return L::get<0>(point) == det
        && L::get<1>(point) == suspend
        && L::get<2>(point) == pin;
}

static_assert(projects_to(ClockSource::Realtime,
    DetSafeTier::WallClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired), "Realtime [FIXED]");
static_assert(projects_to(ClockSource::Monotonic,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::MonotonicRaw,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::Boot,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::NotRequired), "Boot [FIXED]");
static_assert(projects_to(ClockSource::ThreadCpu,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::ProcessCpu,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::TscRaw,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::PerCore), "TscRaw [FIXED]");
static_assert(projects_to(ClockSource::TscSerialized,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::PerCore));
static_assert(projects_to(ClockSource::PmuCounter,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::PerCore));

// ── Order witnesses: Boot ⊏ TscRaw via the pinning axis alone ───────
static_assert( L::leq(cal::clock_source_project(ClockSource::Boot),
                      cal::clock_source_project(ClockSource::TscRaw)));
static_assert(!L::leq(cal::clock_source_project(ClockSource::TscRaw),
                      cal::clock_source_project(ClockSource::Boot)),
    "FIXY-V-184: TscRaw ⋣ Boot — PerCore ⋣ NotRequired; descending FALSE.");
static_assert( L::leq(cal::clock_source_project(ClockSource::Realtime),
                      cal::clock_source_project(ClockSource::Boot)));
static_assert( L::leq(cal::clock_source_project(ClockSource::Monotonic),
                      cal::clock_source_project(ClockSource::Boot)));

// ── Incomparability — proves it is a PRODUCT, not a chain ───────────
static_assert(!L::leq(
    L::make_point(DetSafeTier::Pure, SuspendBehavior::Unknown,
                  PinningRequirement::CrossSocketSafe),
    L::make_point(DetSafeTier::NonDeterministicSyscall, SuspendBehavior::KeepsTicking,
                  PinningRequirement::NotRequired)),
    "FIXY-V-184: these two points are incomparable — a chain cannot do this.");
static_assert(!L::leq(
    L::make_point(DetSafeTier::NonDeterministicSyscall, SuspendBehavior::KeepsTicking,
                  PinningRequirement::NotRequired),
    L::make_point(DetSafeTier::Pure, SuspendBehavior::Unknown,
                  PinningRequirement::CrossSocketSafe)));

// ── join / meet pointwise ───────────────────────────────────────────
static_assert(L::get<1>(L::join(
    cal::clock_source_project(ClockSource::Boot),
    cal::clock_source_project(ClockSource::Realtime))) == SuspendBehavior::KeepsTicking);
static_assert(L::get<2>(L::meet(
    cal::clock_source_project(ClockSource::TscRaw),
    cal::clock_source_project(ClockSource::Boot))) == PinningRequirement::NotRequired);

// ── Monotonic and MonotonicRaw collapse to the SAME tuple ───────────
static_assert(crucible::algebra::equivalent<L>(
    cal::clock_source_project(ClockSource::Monotonic),
    cal::clock_source_project(ClockSource::MonotonicRaw)),
    "FIXY-V-184: Monotonic/MonotonicRaw differ only in NTP-slew, which "
    "this lattice does not model — same projected point; the V-185 "
    "wrapper keeps them distinct at the federation-cache key.");

// ── Names ───────────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"ClockSourceLattice"});
static_assert(cal::clock_source_name(ClockSource::TscRaw) == std::string_view{"TscRaw"});
static_assert(cal::clock_source_name(ClockSource::Boot)   == std::string_view{"Boot"});

// ── EBO note: the grade is NON-empty (3 bytes), so Graded grows ─────
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     L, EightByteValue>)
    <= sizeof(EightByteValue) + 8,
    "FIXY-V-184: the 3-byte (DetSafe×Suspend×Pin) grade plus alignment "
    "padding must fit in 8 trailing bytes over an 8-byte payload.");

}  // namespace

int main() {
    cal::detail::clock_source_lattice_self_test::runtime_smoke_test();
    return 0;
}
