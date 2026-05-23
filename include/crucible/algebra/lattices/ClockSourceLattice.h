#pragma once

// ── crucible::algebra::lattices::ClockSourceLattice ─────────────────
//
// COMPOSITE product lattice over the three independent clock-quality
// axes (Agent 6 §3.1).  Unlike the chain siblings (SuspendBehavior,
// Pinning, SchedulerPolicy) this is NOT a total order — it is the
// componentwise product of three chains:
//
//     ClockSourceLattice
//       = ProductLattice< DetSafeLattice           (replay-safety tier)
//                       , SuspendBehaviorLattice    (pause-on-suspend)
//                       , PinningRequirementLattice (CPU-coherence breadth)
//                       >
//
// The order is POINTWISE: `leq(a, b)` iff a ⊑ b on ALL THREE axes
// simultaneously.  Two clock points that diverge in opposite
// directions on different axes are INCOMPARABLE — exactly what a
// product lattice should express, and what a single chain cannot.
// Birkhoff: the product of distributive chains is distributive, so no
// new axiom work is needed here; the laws lift verbatim from the
// components (proven exhaustively in their own self-tests + here at
// representative clock-source witnesses).
//
// ── The value-level vocabulary: enum class ClockSource ──────────────
//
// The lattice ELEMENT is the 3-tuple (DetSafeTier, SuspendBehavior,
// PinningRequirement).  `ClockSource` is a richer human vocabulary of
// nine concrete Linux/CPU clock sources; each PROJECTS (many-to-one)
// onto a tuple point via `clock_source_project`.  The enum is the
// value-level WITNESS that FIXES the three lattice points for a named
// source — it is NOT itself a lattice carrier (the projection is a
// function, not an order).
//
//     Realtime       CLOCK_REALTIME       — settable wall clock (NTP/admin).
//     Monotonic      CLOCK_MONOTONIC      — NTP-slewed monotonic; pauses on suspend.
//     MonotonicRaw   CLOCK_MONOTONIC_RAW  — un-slewed monotonic; pauses on suspend.
//     Boot           CLOCK_BOOTTIME       — monotonic, suspend-INCLUSIVE.
//     ThreadCpu      CLOCK_THREAD_CPUTIME_ID  — per-thread CPU time.
//     ProcessCpu     CLOCK_PROCESS_CPUTIME_ID — per-process CPU time.
//     TscRaw         RDTSC                — raw cycle counter; per-core domain.
//     TscSerialized  RDTSCP / LFENCE;RDTSC — serialized cycle read; per-core.
//     PmuCounter     perf_event cycles    — per-core PMU counter.
//
// ── Projection table (the three task-FIXED rows are load-bearing) ───
//
//   source         DetSafeTier          SuspendBehavior   PinningRequirement
//   ────────────── ──────────────────── ───────────────── ──────────────────
//   Realtime       WallClockRead        PausesOnSuspend   NotRequired   [FIXED]
//   Monotonic      MonotonicClockRead   PausesOnSuspend   NotRequired
//   MonotonicRaw   MonotonicClockRead   PausesOnSuspend   NotRequired
//   Boot           MonotonicClockRead   KeepsTicking      NotRequired   [FIXED]
//   ThreadCpu      MonotonicClockRead   PausesOnSuspend   NotRequired
//   ProcessCpu     MonotonicClockRead   PausesOnSuspend   NotRequired
//   TscRaw         MonotonicClockRead   KeepsTicking      PerCore       [FIXED]
//   TscSerialized  MonotonicClockRead   KeepsTicking      PerCore
//   PmuCounter     MonotonicClockRead   KeepsTicking      PerCore
//
// Rationale for the derived (non-FIXED) rows:
//   - Realtime reads system_clock — non-monotonic ⇒ WallClockRead; the
//     vDSO read needs no CPU pin ⇒ NotRequired.  (FIXED by task.)
//   - Monotonic / MonotonicRaw are steady_clock-class ⇒ MonotonicClockRead;
//     both STOP during suspend (the documented CLOCK_MONOTONIC behavior —
//     see SuspendBehaviorLattice docblock) ⇒ PausesOnSuspend; vDSO read
//     ⇒ NotRequired.
//   - Boot = CLOCK_BOOTTIME advances through suspend ⇒ KeepsTicking.
//     (FIXED by task.)
//   - ThreadCpu / ProcessCpu accumulate CPU time only while running, so
//     they neither advance during suspend nor migrate-sensitive ⇒
//     MonotonicClockRead + PausesOnSuspend + NotRequired (the kernel
//     tracks the counter regardless of which CPU services the read).
//   - TscRaw / TscSerialized / PmuCounter are per-core cycle counters:
//     invariant-TSC keeps ticking through suspend ⇒ KeepsTicking, but a
//     cross-core read is meaningless without a single-core affinity proof
//     ⇒ PerCore.  (TscRaw FIXED by task; the other two share its domain.)
//
// ── Direction convention (matches every component chain) ────────────
//
// Stronger guarantee = HIGHER on each axis.  Composite `leq(weak,
// strong)` reads "a consumer demanding `weak` on every axis is served
// by a provider that meets `strong` on every axis."  Worked example:
//   Boot   = (MonotonicClockRead, KeepsTicking, NotRequired)
//   TscRaw = (MonotonicClockRead, KeepsTicking, PerCore)
//   ⇒ leq(Boot, TscRaw) is TRUE  (equal on axes 0,1; NotRequired ⊑
//      PerCore on axis 2 — TscRaw's stronger pinning subsumes Boot's
//      requirement), but leq(TscRaw, Boot) is FALSE (PerCore ⋣
//      NotRequired).  TscRaw strictly dominates Boot.
//
// ── This header pulls NO row_hash machinery ─────────────────────────
//
// As BarrierStrengthLattice.h:130 / HwInstructionLattice.h:136 /
// MemoryScopeLattice.h / SuspendBehaviorLattice.h /
// PinningRequirementLattice.h / SchedulerPolicyLattice.h all document:
// the row_hash key is the WRAPPER, never the lattice At<>.  For a
// COMPOSITE the rule is sharper still — the FpMode 11-way composite
// (FpModeProductLattice, FIXY-V-090) carries NO salt of its own; a
// product's federation-cache contribution composes through the
// per-axis component-WRAPPER specializations automatically.  FIXY-V-185
// ships safety/ClockSource.h (the Graded carrier keyed on the
// `ClockSource` NTTP) PLUS the row_hash_contribution<ClockSource<...>>
// discriminator in safety/diag/RowHashFold.h.  `algebra/lattices/` sits
// BELOW `safety/diag/`; keying row_hash on this lattice's product
// element (a nested aggregate, not forward-declarable) would invert that
// layering.
//
//   Axiom coverage:
//     TypeSafe — ClockSource is a strong scoped enum (`enum class :
//                uint8_t`); each projected axis is a strong enum, so
//                cross-axis mixing surfaces at the call site.
//     DetSafe — every operation (inherited leq/join/meet + the
//                projection) is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic AND the
//                runtime smoke test can exercise the projection with a
//                non-constant argument.
//     MemSafe — element_type is the product aggregate; trivially
//                destructible (every component element_type is); no heap.
//   Runtime cost: three single-byte integer compares (one per axis) for
//                 leq; the projection is a 9-arm switch over a 1-byte
//                 enum.  At a fixed type-level source (via the V-185
//                 wrapper) the projection folds away entirely.
//
// See Agent 6 §3.1 for the design rationale; FIXY-V-185 (safety/
// ClockSource.h) for the type-pinned wrapper + row_hash + per-source
// aliases; FIXY-V-201 (topology/Ptp.cpp) for the future PtpHwClock
// enumerator extension (append-only at the next free ordinal).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/DetSafeLattice.h>
#include <crucible/algebra/lattices/PinningRequirementLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── ClockSource — value-level vocabulary of nine concrete sources ───
//
// Ordinal is declaration order ONLY; it carries NO order semantics
// (the order lives in the projected product point, not the enum).
// uint8_t underlying.  FIXY-V-201 appends `PtpHwClock` at the next
// free ordinal (append-only — existing positions never change).
enum class ClockSource : std::uint8_t {
    Realtime      = 0,    // CLOCK_REALTIME — settable wall clock
    Monotonic     = 1,    // CLOCK_MONOTONIC — NTP-slewed, pauses on suspend
    MonotonicRaw  = 2,    // CLOCK_MONOTONIC_RAW — un-slewed, pauses on suspend
    Boot          = 3,    // CLOCK_BOOTTIME — monotonic, suspend-inclusive
    ThreadCpu     = 4,    // CLOCK_THREAD_CPUTIME_ID — per-thread CPU time
    ProcessCpu    = 5,    // CLOCK_PROCESS_CPUTIME_ID — per-process CPU time
    TscRaw        = 6,    // RDTSC — raw per-core cycle counter
    TscSerialized = 7,    // RDTSCP / LFENCE;RDTSC — serialized per-core read
    PmuCounter    = 8,    // perf_event cycles — per-core PMU counter
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t clock_source_count =
    std::meta::enumerators_of(^^ClockSource).size();

[[nodiscard]] consteval std::string_view clock_source_name(ClockSource s) noexcept {
    switch (s) {
        case ClockSource::Realtime:      return "Realtime";
        case ClockSource::Monotonic:     return "Monotonic";
        case ClockSource::MonotonicRaw:  return "MonotonicRaw";
        case ClockSource::Boot:          return "Boot";
        case ClockSource::ThreadCpu:     return "ThreadCpu";
        case ClockSource::ProcessCpu:    return "ProcessCpu";
        case ClockSource::TscRaw:        return "TscRaw";
        case ClockSource::TscSerialized: return "TscSerialized";
        case ClockSource::PmuCounter:    return "PmuCounter";
        default: return std::string_view{"<unknown ClockSource>"};
    }
}

// ── ClockSourceLattice — the 3-axis product (DetSafe × Suspend × Pin) ─
//
// Publicly inherits the N-ary ProductLattice primary at arity 3 (the
// `sizeof...(Ls) != 2` primary, NOT the binary specialization), so it
// gets element_type / leq / join / meet / bottom / top / get<I> /
// nth_lattice<I> / arity for free.  Only `name()` is shadowed to give
// diagnostics the composite's real name instead of the generic
// "Product<L1x...xLn>".
struct ClockSourceLattice
    : ProductLattice<DetSafeLattice, SuspendBehaviorLattice, PinningRequirementLattice> {

    // Named axis projections — mirror first_lattice/second_lattice on
    // the binary case, but spelled for the three clock-quality axes so
    // downstream code (V-185 wrapper) reads intent, not get<0/1/2>.
    using det_safe_axis = DetSafeLattice;
    using suspend_axis  = SuspendBehaviorLattice;
    using pinning_axis  = PinningRequirementLattice;

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "ClockSourceLattice";
    }

    // Build a product point from the three axis enums.  The inherited
    // get<I> returns a reference into the I-th slot whose type IS the
    // component's element_type (each component is a ChainLatticeOps
    // whose element_type is the enum itself).
    [[nodiscard]] static constexpr element_type make_point(
        DetSafeTier det, SuspendBehavior suspend, PinningRequirement pin) noexcept
    {
        element_type point{};
        ClockSourceLattice::get<0>(point) = det;
        ClockSourceLattice::get<1>(point) = suspend;
        ClockSourceLattice::get<2>(point) = pin;
        return point;
    }
};

// ── clock_source_project — the value→tuple FIXING function ──────────
//
// `constexpr` (NOT `consteval`) per the algebra DetSafe convention: it
// folds away at the type level for the V-185 wrapper AND is callable at
// runtime so the mandated runtime smoke test can exercise it with a
// non-constant ClockSource.  Honors the three task-FIXED rows exactly;
// the derived rows follow the projection-table rationale in the
// docblock.
[[nodiscard]] constexpr ClockSourceLattice::element_type
clock_source_project(ClockSource source) noexcept {
    switch (source) {
        case ClockSource::Realtime:
            return ClockSourceLattice::make_point(
                DetSafeTier::WallClockRead, SuspendBehavior::PausesOnSuspend,
                PinningRequirement::NotRequired);
        case ClockSource::Monotonic:
        case ClockSource::MonotonicRaw:
        case ClockSource::ThreadCpu:
        case ClockSource::ProcessCpu:
            return ClockSourceLattice::make_point(
                DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                PinningRequirement::NotRequired);
        case ClockSource::Boot:
            return ClockSourceLattice::make_point(
                DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                PinningRequirement::NotRequired);
        case ClockSource::TscRaw:
        case ClockSource::TscSerialized:
        case ClockSource::PmuCounter:
            return ClockSourceLattice::make_point(
                DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                PinningRequirement::PerCore);
        default:
            // Unreachable for a well-formed ClockSource; bottom is the
            // safe sentinel (weakest on every axis — admits no consumer
            // it shouldn't).
            return ClockSourceLattice::bottom();
    }
}

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::clock_source_lattice_self_test {

// Cardinality + reflection-based name coverage on the value vocabulary.
static_assert(clock_source_count == 9,
    "ClockSource catalog diverged from the nine documented sources; "
    "adding one (e.g. FIXY-V-201 PtpHwClock) requires extending the "
    "clock_source_name() switch AND the clock_source_project() switch "
    "AND bumping this count.");

[[nodiscard]] consteval bool every_clock_source_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ClockSource));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (clock_source_name([:en:]) ==
            std::string_view{"<unknown ClockSource>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_clock_source_has_name(),
    "clock_source_name() switch missing an arm for at least one source — "
    "add the arm or the new source leaks the '<unknown ClockSource>' "
    "sentinel into observer debug output.");

// ── Concept conformance — the composite IS a bounded lattice ────────
static_assert(Lattice<ClockSourceLattice>,
    "FIXY-V-184: ClockSourceLattice must satisfy the Lattice concept "
    "(element_type + leq + join + meet) — inherited from the 3-ary "
    "ProductLattice primary.");
static_assert(BoundedLattice<ClockSourceLattice>,
    "FIXY-V-184: every component (DetSafe/Suspend/Pinning) is a bounded "
    "chain, so the product has bottom() and top().");
static_assert(BoundedBelowLattice<ClockSourceLattice>);
static_assert(BoundedAboveLattice<ClockSourceLattice>);
static_assert(!UnboundedLattice<ClockSourceLattice>);
static_assert(!Semiring<ClockSourceLattice>,
    "FIXY-V-184: ClockSourceLattice carries only order-theoretic ops, "
    "not the equality+add+mul of a semiring.");

// ── Arity / axis projections ────────────────────────────────────────
static_assert(ClockSourceLattice::arity == 3);
static_assert(std::is_same_v<ClockSourceLattice::nth_lattice<0>, DetSafeLattice>);
static_assert(std::is_same_v<ClockSourceLattice::nth_lattice<1>, SuspendBehaviorLattice>);
static_assert(std::is_same_v<ClockSourceLattice::nth_lattice<2>, PinningRequirementLattice>);
static_assert(std::is_same_v<ClockSourceLattice::det_safe_axis, DetSafeLattice>);
static_assert(std::is_same_v<ClockSourceLattice::suspend_axis,  SuspendBehaviorLattice>);
static_assert(std::is_same_v<ClockSourceLattice::pinning_axis,  PinningRequirementLattice>);

// ── Bounds — pointwise lifts of the three component bottoms/tops ────
static_assert(ClockSourceLattice::get<0>(ClockSourceLattice::bottom())
              == DetSafeTier::NonDeterministicSyscall);
static_assert(ClockSourceLattice::get<1>(ClockSourceLattice::bottom())
              == SuspendBehavior::Unknown);
static_assert(ClockSourceLattice::get<2>(ClockSourceLattice::bottom())
              == PinningRequirement::NotRequired);
static_assert(ClockSourceLattice::get<0>(ClockSourceLattice::top())
              == DetSafeTier::Pure);
static_assert(ClockSourceLattice::get<1>(ClockSourceLattice::top())
              == SuspendBehavior::KeepsTicking);
static_assert(ClockSourceLattice::get<2>(ClockSourceLattice::top())
              == PinningRequirement::CrossSocketSafe);

// ════════════════════════════════════════════════════════════════════
// PROJECTION MATRIX — the load-bearing correctness surface (9 × 3)
// ════════════════════════════════════════════════════════════════════
//
// Every ClockSource projects to the documented (DetSafe, Suspend, Pin)
// tuple.  A regression in any cell silently mis-types a clock read —
// the whole reason this lattice exists.

// Tiny per-cell helper keeps the matrix readable.
[[nodiscard]] consteval bool projects_to(
    ClockSource source, DetSafeTier det, SuspendBehavior suspend,
    PinningRequirement pin) noexcept
{
    auto point = clock_source_project(source);
    return ClockSourceLattice::get<0>(point) == det
        && ClockSourceLattice::get<1>(point) == suspend
        && ClockSourceLattice::get<2>(point) == pin;
}

// Row 0 — Realtime [task-FIXED].
static_assert(projects_to(ClockSource::Realtime,
    DetSafeTier::WallClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired),
    "FIXY-V-184: Realtime must project to (WallClockRead, PausesOnSuspend, "
    "NotRequired) — the task-fixed wall-clock row.");
// Rows 1,2 — Monotonic / MonotonicRaw.
static_assert(projects_to(ClockSource::Monotonic,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::MonotonicRaw,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
// Row 3 — Boot [task-FIXED].
static_assert(projects_to(ClockSource::Boot,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::NotRequired),
    "FIXY-V-184: Boot must project to (MonotonicClockRead, KeepsTicking, "
    "NotRequired) — the task-fixed suspend-inclusive row.");
// Rows 4,5 — ThreadCpu / ProcessCpu.
static_assert(projects_to(ClockSource::ThreadCpu,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::ProcessCpu,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
    PinningRequirement::NotRequired));
// Row 6 — TscRaw [task-FIXED].
static_assert(projects_to(ClockSource::TscRaw,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::PerCore),
    "FIXY-V-184: TscRaw must project to (MonotonicClockRead, KeepsTicking, "
    "PerCore) — the task-fixed per-core cycle-counter row.");
// Rows 7,8 — TscSerialized / PmuCounter (share TscRaw's per-core domain).
static_assert(projects_to(ClockSource::TscSerialized,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::PerCore));
static_assert(projects_to(ClockSource::PmuCounter,
    DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
    PinningRequirement::PerCore));

// ── Order witnesses over projected points ───────────────────────────
//
// Boot ⊑ TscRaw: equal on DetSafe + Suspend; NotRequired ⊑ PerCore on
// the pinning axis.  TscRaw strictly dominates Boot.
static_assert(ClockSourceLattice::leq(
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::TscRaw)),
    "FIXY-V-184: Boot ⊑ TscRaw — TscRaw's PerCore pinning subsumes "
    "Boot's NotRequired on the only differing axis.");
static_assert(!ClockSourceLattice::leq(
    clock_source_project(ClockSource::TscRaw),
    clock_source_project(ClockSource::Boot)),
    "FIXY-V-184: TscRaw ⋣ Boot — PerCore ⋣ NotRequired; the descending "
    "direction is FALSE.");
// Realtime is weaker on every axis than Boot ⇒ strictly below it.
static_assert(ClockSourceLattice::leq(
    clock_source_project(ClockSource::Realtime),
    clock_source_project(ClockSource::Boot)));
static_assert(!ClockSourceLattice::leq(
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::Realtime)));
// Monotonic ⊑ Boot (equal DetSafe+Pin; PausesOnSuspend ⊑ KeepsTicking).
static_assert(ClockSourceLattice::leq(
    clock_source_project(ClockSource::Monotonic),
    clock_source_project(ClockSource::Boot)));
static_assert(!ClockSourceLattice::leq(
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::Monotonic)));

// ── INCOMPARABILITY — proves it's a genuine PRODUCT, not a chain ────
//
// Construct two raw product points that diverge in opposite directions:
//   left  = (Pure, Unknown, CrossSocketSafe)  — max DetSafe + max Pin, min Suspend
//   right = (NonDeterministicSyscall, KeepsTicking, NotRequired) — max Suspend, min else
// Neither ⊑ the other: left wins axes 0+2, right wins axis 1.  A chain
// could never produce an incomparable pair.
static_assert(!ClockSourceLattice::leq(
    ClockSourceLattice::make_point(
        DetSafeTier::Pure, SuspendBehavior::Unknown, PinningRequirement::CrossSocketSafe),
    ClockSourceLattice::make_point(
        DetSafeTier::NonDeterministicSyscall, SuspendBehavior::KeepsTicking,
        PinningRequirement::NotRequired)),
    "FIXY-V-184: the product is NOT a chain — these two points are "
    "incomparable (left dominates DetSafe+Pinning, right dominates Suspend).");
static_assert(!ClockSourceLattice::leq(
    ClockSourceLattice::make_point(
        DetSafeTier::NonDeterministicSyscall, SuspendBehavior::KeepsTicking,
        PinningRequirement::NotRequired),
    ClockSourceLattice::make_point(
        DetSafeTier::Pure, SuspendBehavior::Unknown, PinningRequirement::CrossSocketSafe)));

// ── join / meet pointwise across the three axes ─────────────────────
//
// join(Boot, Realtime): max per axis = (max(Mono,Wall)=Mono,
// max(Keeps,Pauses)=Keeps, max(NotReq,NotReq)=NotReq) = Boot's point.
static_assert(ClockSourceLattice::get<0>(ClockSourceLattice::join(
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::Realtime))) == DetSafeTier::MonotonicClockRead);
static_assert(ClockSourceLattice::get<1>(ClockSourceLattice::join(
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::Realtime))) == SuspendBehavior::KeepsTicking);
// meet(TscRaw, Boot): min per axis = (Mono, Keeps, min(PerCore,NotReq)=NotReq).
static_assert(ClockSourceLattice::get<2>(ClockSourceLattice::meet(
    clock_source_project(ClockSource::TscRaw),
    clock_source_project(ClockSource::Boot))) == PinningRequirement::NotRequired);

// ── Lattice-axiom + distributivity rollups at projected witnesses ───
//
// The product machinery's axioms are proven exhaustively over U8³ in
// ProductLattice.h; here we pin that the NAMED composite (with its
// shadowed name() and concrete component chains) also satisfies them at
// representative clock-source points.  Birkhoff guarantees
// distributivity (product of distributive chains).
static_assert(verify_bounded_lattice_axioms_at<ClockSourceLattice>(
    clock_source_project(ClockSource::Realtime),
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::TscRaw)));
static_assert(verify_bounded_lattice_axioms_at<ClockSourceLattice>(
    ClockSourceLattice::bottom(),
    clock_source_project(ClockSource::Monotonic),
    ClockSourceLattice::top()));
static_assert(verify_distributive_lattice<ClockSourceLattice>(
    clock_source_project(ClockSource::Realtime),
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::TscRaw)));

// ── Partial-order sugar from Lattice.h composes with the product ────
static_assert( subsumes<ClockSourceLattice>(
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::TscRaw)));
static_assert( strictly_less<ClockSourceLattice>(
    clock_source_project(ClockSource::Boot),
    clock_source_project(ClockSource::TscRaw)));
static_assert( equivalent<ClockSourceLattice>(
    clock_source_project(ClockSource::Monotonic),
    clock_source_project(ClockSource::MonotonicRaw)),
    "FIXY-V-184: Monotonic and MonotonicRaw project to the SAME tuple — "
    "they differ only in NTP-slew, which this lattice does not model; "
    "the V-185 wrapper keeps them distinct at the federation-cache key.");

// ── Diagnostic names ────────────────────────────────────────────────
static_assert(ClockSourceLattice::name() == std::string_view{"ClockSourceLattice"},
    "FIXY-V-184: name() must be the composite's own name, not the "
    "inherited generic 'Product<L1x...xLn>'.");
static_assert(clock_source_name(ClockSource::TscRaw)        == std::string_view{"TscRaw"});
static_assert(clock_source_name(ClockSource::Boot)          == std::string_view{"Boot"});
static_assert(clock_source_name(ClockSource::TscSerialized) == std::string_view{"TscSerialized"});

// ── Graded composition — the V-185 use case (bounded-shape) ─────────
//
// The product element here is NON-empty (DetSafeTier 1B + SuspendBehavior
// 1B + PinningRequirement 1B = 3 bytes of grade), so Graded over T grows
// by the grade + alignment padding — the EBO-collapse macro does NOT
// apply.  Assert the bounded shape instead (mirrors ProductLattice.h's
// BudgetU8U8 / Budgeted3U8 witnesses).
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using ClockGraded = Graded<ModalityKind::Absolute, ClockSourceLattice, T_>;

static_assert(sizeof(ClockGraded<int>) <= sizeof(int) + 4,
    "FIXY-V-184: ClockGraded<int> exceeded sizeof(int) + 4 — the 3-byte "
    "(DetSafe×Suspend×Pin) grade plus ≤ 1 byte alignment padding must fit "
    "in 4 trailing bytes; if this fires, investigate Graded grade placement "
    "or the per-slot inheritance-EBO discipline in ProductLattice.h.");
static_assert(sizeof(ClockGraded<EightByteValue>) <= sizeof(EightByteValue) + 8,
    "FIXY-V-184: ClockGraded<EightByteValue> exceeded sizeof + 8 — the "
    "3-byte grade plus ≤ 5 bytes alignment padding must fit in 8 trailing "
    "bytes.");

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: exercise the
// projection AND lattice ops AND Graded::weaken / compose with
// NON-CONSTANT arguments — pure static_assert masks consteval-vs-
// constexpr / SFINAE / inline-body bugs.
inline void runtime_smoke_test() {
    // Projection at runtime over a non-constant source.
    ClockSource source = ClockSource::TscRaw;
    auto tsc_point = clock_source_project(source);
    [[maybe_unused]] DetSafeTier        det     = ClockSourceLattice::get<0>(tsc_point);
    [[maybe_unused]] SuspendBehavior    suspend = ClockSourceLattice::get<1>(tsc_point);
    [[maybe_unused]] PinningRequirement pin     = ClockSourceLattice::get<2>(tsc_point);

    // Composite lattice ops at runtime.
    auto boot_point = clock_source_project(ClockSource::Boot);
    [[maybe_unused]] bool le = ClockSourceLattice::leq(boot_point, tsc_point);  // true
    [[maybe_unused]] auto jn = ClockSourceLattice::join(boot_point, tsc_point);
    [[maybe_unused]] auto mt = ClockSourceLattice::meet(boot_point, tsc_point);
    [[maybe_unused]] auto bt = ClockSourceLattice::bottom();
    [[maybe_unused]] auto tp = ClockSourceLattice::top();

    // make_point with non-constant axis values.
    [[maybe_unused]] auto built = ClockSourceLattice::make_point(det, suspend, pin);

    // Graded<Absolute, ClockSourceLattice, T> at runtime — weaken up the
    // pinning axis (Boot ⊑ TscRaw), compose, consume.
    EightByteValue payload{42};
    ClockGraded<EightByteValue> initial{payload, boot_point};
    auto widened  = initial.weaken(tsc_point);              // boot_point ⊑ tsc_point
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(ClockSourceLattice::top());

    [[maybe_unused]] auto grade = rv_widen.grade();
    [[maybe_unused]] auto value = composed.peek().v;
    [[maybe_unused]] auto moved = std::move(composed).consume().v;
}

}  // namespace detail::clock_source_lattice_self_test

}  // namespace crucible::algebra::lattices
