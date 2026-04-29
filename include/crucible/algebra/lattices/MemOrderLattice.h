#pragma once

// ── crucible::algebra::lattices::MemOrderLattice ────────────────────
//
// Five-tier total-order chain lattice over the memory-ordering cost
// spectrum.  The grading axis underlying the MemOrder wrapper from
// 28_04_2026_effects.md §4.3.4 — fourth Month-2 chain lattice
// following DetSafe + HotPath + Wait.
//
// Composes orthogonally with HotPath / Wait via wrapper-nesting
// per 28_04 §4.7.  HotPath bounds WHAT WORK a function does; Wait
// bounds HOW IT WAITS; MemOrder bounds WHAT MEMORY ORDERING its
// atomic ops use.  Together they fence the CLAUDE.md §VI ThreadSafe
// discipline at the type level.
//
// THE LOAD-BEARING USE CASE: CLAUDE.md §VI explicitly bans
// `memory_order_seq_cst` outside narrow exceptions (it forces
// drain-store-buffer + RFO across all CPUs, ~30-100ns vs ~5ns for
// AcqRel).  Today this is review-enforced; with the wrapper, becomes
// a compile error: any function declared in a `requires
// MemOrder::satisfies<AcqRel>` context rejects callees carrying
// `MemOrderTag::SeqCst`.
//
// ── The classification ──────────────────────────────────────────────
//
// Each tier names a CLASS of memory-ordering claim a function makes
// about its atomic operations.  A function declared at tier T
// promises to use ONLY orderings at tier T or STRONGER (cheaper /
// less restrictive) — equivalently, the worst (most expensive)
// ordering it uses is at most T's level.
//
//     Relaxed   — `memory_order_relaxed`.  No fence, no
//                  synchronization.  Atomicity only (single-op
//                  read-modify-write on a single location).
//                  Applicable to: independent counters, statistics
//                  aggregation, load-only reads of immutable data.
//                  ~1-2 cycles on most architectures.
//     Acquire   — `memory_order_acquire` on loads.  One-directional
//                  fence: subsequent loads/stores cannot reorder
//                  upward past the acquire.  Pairs with Release on
//                  the producer side.  ~1-2 cycles + visibility wait.
//     Release   — `memory_order_release` on stores.  One-directional
//                  fence: prior loads/stores cannot reorder downward
//                  past the release.  Pairs with Acquire on the
//                  consumer side.  ~1-2 cycles + flush.
//     AcqRel    — `memory_order_acq_rel` on read-modify-writes.
//                  Combines Acquire (load side) + Release (store
//                  side).  Used for atomic CAS, atomic exchange.
//                  ~5-10 cycles (LOCK CMPXCHG on x86, ldaxr/stlxr
//                  on ARM).
//     SeqCst    — `memory_order_seq_cst`.  Sequential consistency:
//                  total order across ALL atomic ops in the program.
//                  Drain store buffer + global RFO.  ~30-100ns on
//                  modern x86; even more on weakly-ordered ARM.
//                  BANNED on hot path per CLAUDE.md §VI.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class MemOrderTag ∈ the five tiers above.
// Order:   SeqCst ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed.
//
// Bottom = SeqCst   (weakest claim about hardware-friendliness;
//                    uses the most expensive fence; satisfies only
//                    SeqCst-tolerating consumers).
// Top    = Relaxed  (strongest claim — uses no fence at all;
//                    satisfies every consumer asking for at most
//                    a relaxed-tolerant ordering).
// Join   = max      (the more-permissive of two providers).
// Meet   = min      (the more-restrictive of two providers).
//
// ── Direction convention ────────────────────────────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-budget consumer is satisfied by a stronger-budget
// provider" — a Relaxed-tier function can be invoked from any
// consumer site because it makes the strongest "I'm hot-path
// friendly" claim.
//
// This is the Crucible-standard subsumption-up direction, shared
// with the four sister chain lattices (Tolerance / Consistency /
// Lifetime / DetSafe / HotPath / Wait).
//
// ── DIVERGENCES FROM 28_04_2026_effects.md §4.3.4 SPEC ──────────────
//
// (1) ENUM ORDINAL INVERSION.  Spec puts Relaxed=0 ... SeqCst=4
//     (mirroring std::memory_order natural order).  This
//     implementation INVERTS to SeqCst=0 ... Relaxed=4 to keep the
//     lattice's chain direction uniform with the five sister chain
//     lattices.  The SEMANTIC contract from the spec ("any function
//     whose row contains MemOrderTag::SeqCst is rejected from hot-
//     path concept gates") is preserved exactly:
//
//       MemOrder<Relaxed>::satisfies<AcqRel>  = leq(AcqRel, Relaxed)
//                                              = true ✓
//       MemOrder<SeqCst>::satisfies<AcqRel>   = leq(AcqRel, SeqCst)
//                                              = false ✓
//
// (2) LINEARIZATION OF NATURALLY-INCOMPARABLE ELEMENTS.  C++ memory
//     ordering semantics make Acquire and Release naturally
//     INCOMPARABLE — they're orthogonal aspects of single-direction
//     fences (load side vs store side).  But chain lattices require
//     LINEAR ordering.  The chosen linearization (Release ⊑ Acquire,
//     i.e., "a function that uses Acquire is more cheap to admit
//     than one that uses Release") is purely an ARTIFACT of the
//     spec's enum ordinal — it does NOT model a semantic
//     subsumption between the two C++ memory orderings.
//
//     The right semantic interpretation: both Acquire and Release
//     are intermediate cost (one fence each); the chain order
//     between them is for ADMISSION GATING ONLY, not for claiming
//     C++-level interchangeability.  Production callers SHOULD
//     specify the exact memory ordering they want and not rely on
//     `relax<Acquire→Release>` to mean anything semantically beyond
//     "I claimed I needed an acquire-fence-friendly context, but
//     I'm fine being treated as release-fence-friendly here."
//
// (3) NO consume.  Crucible bans `memory_order_consume` per
//     CLAUDE.md §III opt-out ("compilers promote to acquire anyway").
//     The MemOrderTag enum does NOT include a Consume tier; if
//     consume is ever needed, callers use Acquire (matching what
//     the compiler does).
//
//   Axiom coverage:
//     TypeSafe — MemOrderTag is a strong scoped enum (`enum class :
//                uint8_t`); cross-tag mixing requires
//                `std::to_underlying` and is surfaced at the call
//                site.
//     ThreadSafe — THE LOAD-BEARING AXIS.  CLAUDE.md §VI's "never
//                  use seq_cst" discipline becomes a compile-time
//                  fence at every site declared in a hot-path
//                  context.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     five-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level tag via
//     `MemOrderLattice::At<MemOrderTag::Relaxed>` (the conf::Tier
//     pattern), the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors the five sister chain lattices: a per-MemOrderTag
// singleton sub-lattice with empty element_type.
// `Graded<Absolute, MemOrderLattice::At<MemOrderTag::Relaxed>, T>`
// pays zero runtime overhead for the grade itself.
//
// See FOUND-G28 (this file) for the lattice; FOUND-G29
// (safety/MemOrder.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §4.3.4 for the production-call-site
// rationale; CLAUDE.md §VI for the seq_cst ban this lattice fences.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── MemOrderTag — chain over the C++ memory-ordering cost spectrum ──
//
// Ordinal convention: SeqCst=0 (bottom) ... Relaxed=4 (top), matching
// the project convention (bottom=0).  This INVERTS the spec's
// ordinal hint (which mirrors std::memory_order); semantic contract
// preserved.  Note: `consume` is deliberately omitted — see lattice
// docblock divergence (3).
enum class MemOrderTag : std::uint8_t {
    SeqCst  = 0,    // bottom: total-order fence (most expensive)
    AcqRel  = 1,    // RMW combined Acquire+Release
    Release = 2,    // store-side directional fence
    Acquire = 3,    // load-side directional fence
    Relaxed = 4,    // top: atomicity only, no fence (cheapest)
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t mem_order_tag_count =
    std::meta::enumerators_of(^^MemOrderTag).size();

[[nodiscard]] consteval std::string_view mem_order_tag_name(MemOrderTag t) noexcept {
    switch (t) {
        case MemOrderTag::SeqCst:  return "SeqCst";
        case MemOrderTag::AcqRel:  return "AcqRel";
        case MemOrderTag::Release: return "Release";
        case MemOrderTag::Acquire: return "Acquire";
        case MemOrderTag::Relaxed: return "Relaxed";
        default:                   return std::string_view{"<unknown MemOrderTag>"};
    }
}

// ── Full MemOrderLattice (chain order) ──────────────────────────────
struct MemOrderLattice : ChainLatticeOps<MemOrderTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return MemOrderTag::SeqCst;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return MemOrderTag::Relaxed;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "MemOrderLattice";
    }

    // ── At<T>: singleton sub-lattice at a fixed type-level tag ─────
    template <MemOrderTag T>
    struct At {
        struct element_type {
            using mem_order_tag_value_type = MemOrderTag;
            [[nodiscard]] constexpr operator mem_order_tag_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr MemOrderTag tag = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case MemOrderTag::SeqCst:  return "MemOrderLattice::At<SeqCst>";
                case MemOrderTag::AcqRel:  return "MemOrderLattice::At<AcqRel>";
                case MemOrderTag::Release: return "MemOrderLattice::At<Release>";
                case MemOrderTag::Acquire: return "MemOrderLattice::At<Acquire>";
                case MemOrderTag::Relaxed: return "MemOrderLattice::At<Relaxed>";
                default:                   return "MemOrderLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace mem_order_tag {
    using SeqCstTag  = MemOrderLattice::At<MemOrderTag::SeqCst>;
    using AcqRelTag  = MemOrderLattice::At<MemOrderTag::AcqRel>;
    using ReleaseTag = MemOrderLattice::At<MemOrderTag::Release>;
    using AcquireTag = MemOrderLattice::At<MemOrderTag::Acquire>;
    using RelaxedTag = MemOrderLattice::At<MemOrderTag::Relaxed>;
}  // namespace mem_order_tag

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::mem_order_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(mem_order_tag_count == 5,
    "MemOrderTag catalog diverged from {SeqCst, AcqRel, Release, "
    "Acquire, Relaxed}; confirm intent and update the dispatcher's "
    "hot-path admission gates.");

[[nodiscard]] consteval bool every_mem_order_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^MemOrderTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (mem_order_tag_name([:en:]) ==
            std::string_view{"<unknown MemOrderTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_mem_order_tag_has_name(),
    "mem_order_tag_name() switch missing arm for at least one tag.");

// Concept conformance — full lattice + each At<T> sub-lattice.
static_assert(Lattice<MemOrderLattice>);
static_assert(BoundedLattice<MemOrderLattice>);
static_assert(Lattice<mem_order_tag::SeqCstTag>);
static_assert(Lattice<mem_order_tag::AcqRelTag>);
static_assert(Lattice<mem_order_tag::ReleaseTag>);
static_assert(Lattice<mem_order_tag::AcquireTag>);
static_assert(Lattice<mem_order_tag::RelaxedTag>);
static_assert(BoundedLattice<mem_order_tag::RelaxedTag>);

// Negative concept assertions.
static_assert(!UnboundedLattice<MemOrderLattice>);
static_assert(!Semiring<MemOrderLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<mem_order_tag::RelaxedTag::element_type>);
static_assert(std::is_empty_v<mem_order_tag::AcqRelTag::element_type>);
static_assert(std::is_empty_v<mem_order_tag::SeqCstTag::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (MemOrderTag)³ = 125 triples each.
static_assert(verify_chain_lattice_exhaustive<MemOrderLattice>(),
    "MemOrderLattice's chain-order lattice axioms must hold at every "
    "(MemOrderTag)³ triple — failure indicates a defect in "
    "leq/join/meet or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<MemOrderLattice>(),
    "MemOrderLattice's chain order must satisfy distributivity at "
    "every (MemOrderTag)³ triple.");

// Direct order witnesses — Relaxed at the top (cheapest) and SeqCst
// at the bottom (most expensive).
static_assert( MemOrderLattice::leq(MemOrderTag::SeqCst,  MemOrderTag::AcqRel));
static_assert( MemOrderLattice::leq(MemOrderTag::AcqRel,  MemOrderTag::Release));
static_assert( MemOrderLattice::leq(MemOrderTag::Release, MemOrderTag::Acquire));
static_assert( MemOrderLattice::leq(MemOrderTag::Acquire, MemOrderTag::Relaxed));
static_assert( MemOrderLattice::leq(MemOrderTag::SeqCst,  MemOrderTag::Relaxed)); // transitive
static_assert(!MemOrderLattice::leq(MemOrderTag::Relaxed, MemOrderTag::SeqCst));
static_assert(!MemOrderLattice::leq(MemOrderTag::Relaxed, MemOrderTag::Acquire));
static_assert(!MemOrderLattice::leq(MemOrderTag::AcqRel,  MemOrderTag::SeqCst));

// Pin bottom / top to chain endpoints.
static_assert(MemOrderLattice::bottom() == MemOrderTag::SeqCst);
static_assert(MemOrderLattice::top()    == MemOrderTag::Relaxed);

// Join strengthens (max); meet weakens (min).
static_assert(MemOrderLattice::join(MemOrderTag::SeqCst, MemOrderTag::Relaxed)
              == MemOrderTag::Relaxed);
static_assert(MemOrderLattice::join(MemOrderTag::AcqRel, MemOrderTag::Release)
              == MemOrderTag::Release);
static_assert(MemOrderLattice::meet(MemOrderTag::SeqCst, MemOrderTag::Relaxed)
              == MemOrderTag::SeqCst);
static_assert(MemOrderLattice::meet(MemOrderTag::Acquire, MemOrderTag::Relaxed)
              == MemOrderTag::Acquire);

// Diagnostic names.
static_assert(MemOrderLattice::name() == "MemOrderLattice");
static_assert(mem_order_tag::SeqCstTag::name()  == "MemOrderLattice::At<SeqCst>");
static_assert(mem_order_tag::AcqRelTag::name()  == "MemOrderLattice::At<AcqRel>");
static_assert(mem_order_tag::ReleaseTag::name() == "MemOrderLattice::At<Release>");
static_assert(mem_order_tag::AcquireTag::name() == "MemOrderLattice::At<Acquire>");
static_assert(mem_order_tag::RelaxedTag::name() == "MemOrderLattice::At<Relaxed>");

// Reflection-driven coverage check on At<T>::name().
[[nodiscard]] consteval bool every_at_mem_order_tag_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^MemOrderTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (MemOrderLattice::At<([:en:])>::name() ==
            std::string_view{"MemOrderLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_mem_order_tag_has_name(),
    "MemOrderLattice::At<T>::name() switch missing an arm for at "
    "least one tag.");

// Convenience aliases resolve correctly.
static_assert(mem_order_tag::SeqCstTag::tag  == MemOrderTag::SeqCst);
static_assert(mem_order_tag::AcqRelTag::tag  == MemOrderTag::AcqRel);
static_assert(mem_order_tag::ReleaseTag::tag == MemOrderTag::Release);
static_assert(mem_order_tag::AcquireTag::tag == MemOrderTag::Acquire);
static_assert(mem_order_tag::RelaxedTag::tag == MemOrderTag::Relaxed);

// ── Layout invariants on Graded<...,At<T>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// RelaxedTag — most semantically-loaded (relaxed atomicity-only;
// admits at any consumer).  Production: TraceRing head/tail
// counters reading their OWN atomic.
template <typename T_>
using RelaxedGraded = Graded<ModalityKind::Absolute, mem_order_tag::RelaxedTag, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, double);

// AcqRelTag — RMW atomic CAS.  Production: KernelCache slot CAS.
template <typename T_>
using AcqRelGraded = Graded<ModalityKind::Absolute, mem_order_tag::AcqRelTag, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelGraded, EightByteValue);

// SeqCstTag — banned on hot path; admits only SeqCst-tolerating
// consumers.
template <typename T_>
using SeqCstGraded = Graded<ModalityKind::Absolute, mem_order_tag::SeqCstTag, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    MemOrderTag a = MemOrderTag::SeqCst;
    MemOrderTag b = MemOrderTag::Relaxed;
    [[maybe_unused]] bool        l1   = MemOrderLattice::leq(a, b);
    [[maybe_unused]] MemOrderTag j1   = MemOrderLattice::join(a, b);
    [[maybe_unused]] MemOrderTag m1   = MemOrderLattice::meet(a, b);
    [[maybe_unused]] MemOrderTag bot  = MemOrderLattice::bottom();
    [[maybe_unused]] MemOrderTag topv = MemOrderLattice::top();

    // Mid-tier ops — chain through the Acquire/Release boundary.
    MemOrderTag acq = MemOrderTag::Acquire;
    MemOrderTag rel = MemOrderTag::Release;
    [[maybe_unused]] MemOrderTag j2 = MemOrderLattice::join(acq, rel);   // Acquire (top of pair)
    [[maybe_unused]] MemOrderTag m2 = MemOrderLattice::meet(acq, rel);   // Release (bottom of pair)

    // Graded<Absolute, RelaxedTag, T> at runtime.
    OneByteValue v{42};
    RelaxedGraded<OneByteValue> initial{v, mem_order_tag::RelaxedTag::bottom()};
    auto widened   = initial.weaken(mem_order_tag::RelaxedTag::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(mem_order_tag::RelaxedTag::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<MemOrderTag>::element_type → MemOrderTag.
    mem_order_tag::RelaxedTag::element_type e{};
    [[maybe_unused]] MemOrderTag rec = e;
}

}  // namespace detail::mem_order_lattice_self_test

}  // namespace crucible::algebra::lattices
