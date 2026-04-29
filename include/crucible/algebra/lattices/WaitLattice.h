#pragma once

// ── crucible::algebra::lattices::WaitLattice ────────────────────────
//
// Six-tier total-order chain lattice over the wait-strategy spectrum.
// The grading axis underlying the Wait wrapper from
// 28_04_2026_effects.md §4.3.3 — third Month-2 chain lattice
// following DetSafe + HotPath.
//
// Composes orthogonally with HotPath via wrapper-nesting per 28_04
// §4.7.  HotPath bounds WHAT WORK a function does (alloc / syscall /
// block); Wait bounds HOW IT WAITS (spin / futex / syscall).  The
// canonical foreground hot-path waiter:
//
//     HotPath<Hot, Wait<SpinPause, T>>
//
// — a function admitted to the foreground hot path AND constrained
// to use only the cheapest wait strategy (~10-40ns intra-socket via
// MESI cache-line invalidation, per CLAUDE.md §IX.5).
//
// Closes the design gap tracked by CONCURRENT-DELAYS (#555).
//
// ── The classification ──────────────────────────────────────────────
//
// Each tier names a CLASS of cross-thread / cross-core waiting
// mechanism.  A function declared at tier T promises to use ONLY
// wait operations admissible at tier T (or at a STRONGER tier —
// stronger budgets are admissible in weaker contexts).
//
// Per CLAUDE.md §IX.5 latency hierarchy (intra-socket; cross-socket
// adds ~30-100ns for the UPI/QPI hop):
//
//     SpinPause     — `_mm_pause` (x86) / `yield` (ARM) on an
//                      acquire-load.  10-40ns wait floor set by
//                      MESI cache-line transfer.  No kernel
//                      transition, no power-state change, no
//                      system call.  The CHEAPEST wait strategy.
//                      Production: TraceRing / MetaLog SPSC ring
//                      try_pop polls.
//     BoundedSpin   — Same as SpinPause + exponential backoff.
//                      10ns – 1μs.  Used when expected wait is
//                      unknown but bounded (rare in Crucible —
//                      most waits are imminent-event signals).
//     UmwaitC01     — Intel WAITPKG `umonitor` + `umwait` (Tremont+,
//                      Sapphire Rapids+).  ~100-500ns + wait time;
//                      power-aware spin.  Useful when expected wait
//                      is 1-100μs.  Not on Crucible's hot path
//                      today (no architectural waits in that range
//                      on a well-designed hot loop).
//     AcquireWait   — `std::atomic::wait` / raw `futex(FUTEX_WAIT)`.
//                      1-5μs (Linux futex syscall + scheduler
//                      decision).  Banned on hot path per CLAUDE.md
//                      §IX.5; admissible at warm-tier sync points.
//     Park          — `std::condition_variable::wait` /
//                      `pthread_cond_wait`.  3-10μs.  Crucible uses
//                      `std::jthread` which encapsulates this for
//                      graceful join only.
//     Block         — `poll` / `epoll_wait` / `read(blocking_fd)` /
//                      arbitrary syscall blocking.  5-20μs+.
//                      Cipher fsync, TraceLoader I/O, Canopy
//                      gossip wire reads.  The MOST EXPENSIVE wait.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class WaitStrategy ∈ the six tiers above.
// Order:   Block ⊑ Park ⊑ AcquireWait ⊑ UmwaitC01 ⊑ BoundedSpin
//                ⊑ SpinPause.
//
// Bottom = Block      (weakest claim — may block on syscall;
//                      satisfies only Block-tolerating consumers).
// Top    = SpinPause  (strongest claim — never blocks; satisfies
//                      every consumer).
// Join   = max        (the more-constrained wait of two providers).
// Meet   = min        (the less-constrained wait of two providers).
//
// ── Direction convention (matches Tolerance / Consistency / Lifetime
//                          / DetSafe / HotPath) ────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-budget consumer is satisfied by a stronger-budget
// provider" — a SpinPause-tier function can be invoked from any
// waiter site because it never reaches a kernel transition.
//
// This is the Crucible-standard subsumption-up direction, shared
// with the four sister chain lattices.
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.3 SPEC ──────────────
//
// The spec's enum ordinals (SpinPause=0, BoundedSpin=1, ...,
// Block=5) put SpinPause at the BOTTOM of the chain by ordinal.
// This implementation INVERTS that ordering (Block=0, ...,
// SpinPause=5) to keep the lattice's chain direction uniform with
// Tolerance / Consistency / Lifetime / DetSafe / HotPath — which
// all put the strongest constraint at the TOP.  The SEMANTIC
// contract from the spec ("SpinPause-tier values are admissible
// everywhere") is preserved exactly:
//
//   Wait<SpinPause>::satisfies<Park>      = leq(Park, SpinPause)
//                                          = true ✓
//   Wait<SpinPause>::satisfies<Block>     = leq(Block, SpinPause)
//                                          = true ✓
//   Wait<Block>::satisfies<SpinPause>     = leq(SpinPause, Block)
//                                          = false ✓
//   Wait<Block>::satisfies<Block>         = leq(Block, Block)
//                                          = true ✓
//
// The only effect of the inversion is enum-encoding — SpinPause ==
// uint8_t{5} rather than uint8_t{0}.  Production callers using the
// enum names (the vast majority) see no difference.  Mirrors the
// DetSafe + HotPath direction inversions.
//
//   Axiom coverage:
//     TypeSafe — WaitStrategy is a strong scoped enum (`enum class
//                : uint8_t`); cross-tier mixing requires
//                `std::to_underlying` and is surfaced at the call
//                site.
//     DetSafe — every operation is `constexpr` (NOT `consteval`)
//                so Graded's runtime `pre (L::leq(...))`
//                precondition can fire under the `enforce`
//                contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     six-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level tier via
//     `WaitLattice::At<WaitStrategy::SpinPause>` (the conf::Tier
//     pattern), the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors ConfLattice::At<Conf>, ToleranceLattice::At<Tolerance>,
// ConsistencyLattice::At<Consistency>, DetSafeLattice::At<DetSafeTier>,
// HotPathLattice::At<HotPathTier>: a per-WaitStrategy singleton
// sub-lattice with empty element_type.
// `Graded<Absolute, WaitLattice::At<WaitStrategy::SpinPause>, T>`
// pays zero runtime overhead for the grade itself.
//
// See FOUND-G23 (this file) for the lattice; FOUND-G24
// (safety/Wait.h) for the type-pinned wrapper; 28_04_2026_effects.md
// §4.3.3 for the production-call-site rationale; CLAUDE.md §IX.5
// for the latency hierarchy this lattice grades over.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── WaitStrategy — chain over the wait-mechanism cost spectrum ──────
//
// Ordinal convention: Block=0 (bottom) ... SpinPause=5 (top),
// matching the project convention (bottom=0).  This INVERTS the
// 28_04 §4.3.3 spec's ordinal hint; semantic contract preserved.
enum class WaitStrategy : std::uint8_t {
    Block       = 0,    // bottom: poll / epoll_wait / blocking syscall
    Park        = 1,    // pthread_cond_wait / std::condition_variable
    AcquireWait = 2,    // std::atomic::wait / futex(FUTEX_WAIT)
    UmwaitC01   = 3,    // Intel WAITPKG umonitor+umwait (Tremont+)
    BoundedSpin = 4,    // SpinPause + exponential backoff
    SpinPause   = 5,    // top: _mm_pause / yield on acquire-load
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t wait_strategy_count =
    std::meta::enumerators_of(^^WaitStrategy).size();

[[nodiscard]] consteval std::string_view wait_strategy_name(WaitStrategy s) noexcept {
    switch (s) {
        case WaitStrategy::Block:       return "Block";
        case WaitStrategy::Park:        return "Park";
        case WaitStrategy::AcquireWait: return "AcquireWait";
        case WaitStrategy::UmwaitC01:   return "UmwaitC01";
        case WaitStrategy::BoundedSpin: return "BoundedSpin";
        case WaitStrategy::SpinPause:   return "SpinPause";
        default:                        return std::string_view{"<unknown WaitStrategy>"};
    }
}

// ── Full WaitLattice (chain order) ──────────────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<WaitStrategy>.
struct WaitLattice : ChainLatticeOps<WaitStrategy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return WaitStrategy::Block;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return WaitStrategy::SpinPause;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "WaitLattice";
    }

    // ── At<T>: singleton sub-lattice at a fixed type-level strategy ─
    template <WaitStrategy T>
    struct At {
        struct element_type {
            using wait_strategy_value_type = WaitStrategy;
            [[nodiscard]] constexpr operator wait_strategy_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr WaitStrategy strategy = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case WaitStrategy::Block:       return "WaitLattice::At<Block>";
                case WaitStrategy::Park:        return "WaitLattice::At<Park>";
                case WaitStrategy::AcquireWait: return "WaitLattice::At<AcquireWait>";
                case WaitStrategy::UmwaitC01:   return "WaitLattice::At<UmwaitC01>";
                case WaitStrategy::BoundedSpin: return "WaitLattice::At<BoundedSpin>";
                case WaitStrategy::SpinPause:   return "WaitLattice::At<SpinPause>";
                default:                        return "WaitLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace wait_strategy {
    using BlockStrategy       = WaitLattice::At<WaitStrategy::Block>;
    using ParkStrategy        = WaitLattice::At<WaitStrategy::Park>;
    using AcquireWaitStrategy = WaitLattice::At<WaitStrategy::AcquireWait>;
    using UmwaitC01Strategy   = WaitLattice::At<WaitStrategy::UmwaitC01>;
    using BoundedSpinStrategy = WaitLattice::At<WaitStrategy::BoundedSpin>;
    using SpinPauseStrategy   = WaitLattice::At<WaitStrategy::SpinPause>;
}  // namespace wait_strategy

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::wait_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(wait_strategy_count == 6,
    "WaitStrategy catalog diverged from {Block, Park, AcquireWait, "
    "UmwaitC01, BoundedSpin, SpinPause}; confirm intent and update "
    "the dispatcher's wait-admission gates + scheduler-policy "
    "plumbing.");

[[nodiscard]] consteval bool every_wait_strategy_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^WaitStrategy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (wait_strategy_name([:en:]) ==
            std::string_view{"<unknown WaitStrategy>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_wait_strategy_has_name(),
    "wait_strategy_name() switch missing arm for at least one tier — "
    "add the arm or the new tier leaks the '<unknown WaitStrategy>' "
    "sentinel into Augur's debug output.");

// Concept conformance — full lattice + each At<T> sub-lattice.
static_assert(Lattice<WaitLattice>);
static_assert(BoundedLattice<WaitLattice>);
static_assert(Lattice<wait_strategy::BlockStrategy>);
static_assert(Lattice<wait_strategy::ParkStrategy>);
static_assert(Lattice<wait_strategy::AcquireWaitStrategy>);
static_assert(Lattice<wait_strategy::UmwaitC01Strategy>);
static_assert(Lattice<wait_strategy::BoundedSpinStrategy>);
static_assert(Lattice<wait_strategy::SpinPauseStrategy>);
static_assert(BoundedLattice<wait_strategy::SpinPauseStrategy>);

// Negative concept assertions — pin WaitLattice's character.
static_assert(!UnboundedLattice<WaitLattice>);
static_assert(!Semiring<WaitLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<wait_strategy::SpinPauseStrategy::element_type>);
static_assert(std::is_empty_v<wait_strategy::BoundedSpinStrategy::element_type>);
static_assert(std::is_empty_v<wait_strategy::ParkStrategy::element_type>);
static_assert(std::is_empty_v<wait_strategy::BlockStrategy::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (WaitStrategy)³ = 216 triples each.
static_assert(verify_chain_lattice_exhaustive<WaitLattice>(),
    "WaitLattice's chain-order lattice axioms must hold at every "
    "(WaitStrategy)³ triple — failure indicates a defect in "
    "leq/join/meet or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<WaitLattice>(),
    "WaitLattice's chain order must satisfy distributivity at every "
    "(WaitStrategy)³ triple.");

// Direct order witnesses — the entire chain is increasing, with
// SpinPause at the top (cheapest wait) and Block at the bottom.
static_assert( WaitLattice::leq(WaitStrategy::Block,       WaitStrategy::Park));
static_assert( WaitLattice::leq(WaitStrategy::Park,        WaitStrategy::AcquireWait));
static_assert( WaitLattice::leq(WaitStrategy::AcquireWait, WaitStrategy::UmwaitC01));
static_assert( WaitLattice::leq(WaitStrategy::UmwaitC01,   WaitStrategy::BoundedSpin));
static_assert( WaitLattice::leq(WaitStrategy::BoundedSpin, WaitStrategy::SpinPause));
static_assert( WaitLattice::leq(WaitStrategy::Block,       WaitStrategy::SpinPause));   // transitive
static_assert(!WaitLattice::leq(WaitStrategy::SpinPause,   WaitStrategy::Block));
static_assert(!WaitLattice::leq(WaitStrategy::SpinPause,   WaitStrategy::BoundedSpin));
static_assert(!WaitLattice::leq(WaitStrategy::Park,        WaitStrategy::Block));

// Pin bottom / top to the chain endpoints.
static_assert(WaitLattice::bottom() == WaitStrategy::Block);
static_assert(WaitLattice::top()    == WaitStrategy::SpinPause);

// Join strengthens (max); meet weakens (min).
static_assert(WaitLattice::join(WaitStrategy::Block,       WaitStrategy::SpinPause)
              == WaitStrategy::SpinPause);
static_assert(WaitLattice::join(WaitStrategy::Park,        WaitStrategy::AcquireWait)
              == WaitStrategy::AcquireWait);
static_assert(WaitLattice::meet(WaitStrategy::Block,       WaitStrategy::SpinPause)
              == WaitStrategy::Block);
static_assert(WaitLattice::meet(WaitStrategy::BoundedSpin, WaitStrategy::SpinPause)
              == WaitStrategy::BoundedSpin);

// Diagnostic names.
static_assert(WaitLattice::name() == "WaitLattice");
static_assert(wait_strategy::BlockStrategy::name()       == "WaitLattice::At<Block>");
static_assert(wait_strategy::ParkStrategy::name()        == "WaitLattice::At<Park>");
static_assert(wait_strategy::AcquireWaitStrategy::name() == "WaitLattice::At<AcquireWait>");
static_assert(wait_strategy::UmwaitC01Strategy::name()   == "WaitLattice::At<UmwaitC01>");
static_assert(wait_strategy::BoundedSpinStrategy::name() == "WaitLattice::At<BoundedSpin>");
static_assert(wait_strategy::SpinPauseStrategy::name()   == "WaitLattice::At<SpinPause>");

// Reflection-driven coverage check on At<T>::name().
[[nodiscard]] consteval bool every_at_wait_strategy_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^WaitStrategy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (WaitLattice::At<([:en:])>::name() ==
            std::string_view{"WaitLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_wait_strategy_has_name(),
    "WaitLattice::At<T>::name() switch missing an arm for at least "
    "one strategy.");

// Convenience aliases resolve correctly.
static_assert(wait_strategy::BlockStrategy::strategy       == WaitStrategy::Block);
static_assert(wait_strategy::ParkStrategy::strategy        == WaitStrategy::Park);
static_assert(wait_strategy::AcquireWaitStrategy::strategy == WaitStrategy::AcquireWait);
static_assert(wait_strategy::UmwaitC01Strategy::strategy   == WaitStrategy::UmwaitC01);
static_assert(wait_strategy::BoundedSpinStrategy::strategy == WaitStrategy::BoundedSpin);
static_assert(wait_strategy::SpinPauseStrategy::strategy   == WaitStrategy::SpinPause);

// ── Layout invariants on Graded<...,At<T>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// SpinPauseStrategy — the most semantically-loaded tier (foreground
// SPSC ring waiter + Vessel hot-path wait sites).
template <typename T_>
using SpinPauseGraded = Graded<ModalityKind::Absolute, wait_strategy::SpinPauseStrategy, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinPauseGraded, double);

// ParkStrategy — cross-thread join points (jthread destructor).
template <typename T_>
using ParkGraded = Graded<ModalityKind::Absolute, wait_strategy::ParkStrategy, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ParkGraded, EightByteValue);

// BlockStrategy — Cipher fsync, Canopy gossip wire reads.
template <typename T_>
using BlockGraded = Graded<ModalityKind::Absolute, wait_strategy::BlockStrategy, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BlockGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Full WaitLattice ops at runtime.
    WaitStrategy a = WaitStrategy::Block;
    WaitStrategy b = WaitStrategy::SpinPause;
    [[maybe_unused]] bool         l1   = WaitLattice::leq(a, b);
    [[maybe_unused]] WaitStrategy j1   = WaitLattice::join(a, b);
    [[maybe_unused]] WaitStrategy m1   = WaitLattice::meet(a, b);
    [[maybe_unused]] WaitStrategy bot  = WaitLattice::bottom();
    [[maybe_unused]] WaitStrategy topv = WaitLattice::top();

    // Mid-tier ops — chain through the WAITPKG/futex boundary.
    WaitStrategy umwait  = WaitStrategy::UmwaitC01;
    WaitStrategy futex   = WaitStrategy::AcquireWait;
    [[maybe_unused]] WaitStrategy j2 = WaitLattice::join(umwait, futex);   // UmwaitC01 (more pure)
    [[maybe_unused]] WaitStrategy m2 = WaitLattice::meet(umwait, futex);   // AcquireWait

    // Graded<Absolute, SpinPauseStrategy, T> at runtime.
    OneByteValue v{42};
    SpinPauseGraded<OneByteValue> initial{v, wait_strategy::SpinPauseStrategy::bottom()};
    auto widened   = initial.weaken(wait_strategy::SpinPauseStrategy::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(wait_strategy::SpinPauseStrategy::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<WaitStrategy>::element_type → WaitStrategy.
    wait_strategy::SpinPauseStrategy::element_type e{};
    [[maybe_unused]] WaitStrategy rec = e;
}

}  // namespace detail::wait_lattice_self_test

}  // namespace crucible::algebra::lattices
