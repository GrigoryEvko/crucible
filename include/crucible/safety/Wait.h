#pragma once

// ── crucible::safety::Wait<WaitStrategy_v Strategy, T> ──────────────
//
// Type-pinned wait-strategy wrapper.  A value of type T whose
// permitted wait mechanism (Block ⊑ Park ⊑ AcquireWait ⊑ UmwaitC01
// ⊑ BoundedSpin ⊑ SpinPause) is fixed at the type level via the
// non-type template parameter Strategy.  Third Month-2 chain
// wrapper from 28_04_2026_effects.md §4.3.3 (FOUND-G24).  Closes
// the design gap tracked by CONCURRENT-DELAYS (#555).
//
// Composes orthogonally with HotPath via wrapper-nesting:
//
//   HotPath<Hot, Wait<SpinPause, T>>
//
// — a foreground-hot-path-safe value constrained to use only the
// cheapest wait mechanism (~10-40ns intra-socket via MESI).  The
// dispatcher reads BOTH axes; hot-path-only call sites refuse Wait
// strategies above the SpinPause/BoundedSpin boundary.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     WaitLattice::At<Strategy>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Strategy>::element_type
//                 is empty, sizeof(Wait<Strategy, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.3):
//     - SPSC ring waiters declare `SpinPause`
//     - Bounded retry loops declare `BoundedSpin`
//     - Power-aware spinners (1-100μs expected wait) declare
//       `UmwaitC01`
//     - std::atomic::wait sites declare `AcquireWait`
//     - jthread join points declare `Park`
//     - Cipher fsync, Canopy gossip wire reads declare `Block`
//
//   The bug class caught: a Park (futex, μs latency) called from a
//   hot-path context.  Today caught by review (CLAUDE.md §IX.5
//   discipline) or perf regression hours later; with the wrapper,
//   becomes a compile error at the call boundary because the caller
//   declared `requires Wait::satisfies<SpinPause>` and the callee
//   carrying Park (or weaker) fails the gate.
//
//   Axiom coverage:
//     TypeSafe — WaitStrategy_v is a strong scoped enum;
//                cross-strategy mismatches are compile errors via
//                the relax<WeakerStrategy>() and
//                satisfies<RequiredStrategy> gates.
//     ThreadSafe — Wait makes the discipline at CLAUDE.md §IX.5
//                  visible at the type level: the dispatcher refuses
//                  to admit Park/Block-tier callees into hot-path
//                  contexts.
//     MemSafe — defaulted copy/move; T's move semantics carry
//               through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(Wait<Strategy, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Strategy>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A wait-strategy pin is a STATIC property of WHAT MECHANISM the
// function uses to wait — not a context the value lives in.
// Mirrors NumericalTier / Consistency / OpaqueLifetime / DetSafe /
// HotPath — all Absolute modalities over At<>-pinned grades.
//
// ── Strategy-conversion API: relax + satisfies ─────────────────────
//
// Wait subsumption-direction (per WaitLattice.h docblock):
//
//   leq(weaker, stronger) reads "weaker-budget is below stronger-
//   budget in the lattice."
//   Bottom = Block (weakest); Top = SpinPause (strongest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER strategy (SpinPause) satisfies a
//   consumer at a WEAKER strategy (Park).  Stronger wait-discipline
//   serves weaker requirement.  A Wait<SpinPause, T> can be
//   relaxed to Wait<Park, T> — the spin-pause-bound value
//   trivially satisfies the Park-acceptance gate (it doesn't park,
//   so it certainly doesn't park more than allowed).
//
//   The converse is forbidden: a Wait<Block, T> CANNOT become a
//   Wait<SpinPause, T> — the block-tier value performs a syscall
//   that the spin-pause discipline forbids; relaxing the type to
//   claim SpinPause compliance would defeat the hot-path discipline.
//   No `tighten()` method exists; the only way to obtain a
//   Wait<SpinPause, T> is to construct one at a genuinely-spin-pause
//   call site (e.g., SPSC ring try_pop loop body).
//
// API:
//
//   - relax<WeakerStrategy>() &  / && — convert to a less-strict
//                                       strategy; compile error if
//                                       WeakerStrategy > Strategy
//                                       (would CLAIM more wait-
//                                       discipline than the source
//                                       provides).
//   - satisfies<RequiredStrategy>     — static predicate: does this
//                                       wrapper's pinned strategy
//                                       subsume the required
//                                       strategy?  Equivalent to
//                                       leq(RequiredStrategy,
//                                       Strategy).
//   - strategy (static constexpr)     — the pinned WaitStrategy_v
//                                       value.
//
// SEMANTIC NOTE on the "relax" naming: for Wait, "weakening the
// strategy" means accepting MORE expensive wait mechanisms (going
// down the chain SpinPause ← BoundedSpin ← ... ← Block).  Calling
// `relax<Park>()` on a SpinPause-pinned value means "I'm OK
// treating this spin-only value as Park-tolerable here."  This is
// a downgrade of the WAIT-DISCIPLINE guarantee.  The API uses
// `relax` for uniformity with NumericalTier / Consistency /
// OpaqueLifetime / DetSafe / HotPath.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned strategy and would be the LOAD-BEARING BUG: a
// Block-tier value claiming SpinPause compliance would defeat the
// hot-path discipline.  Hidden by the wrapper.
//
// See FOUND-G23 (algebra/lattices/WaitLattice.h) for the underlying
// substrate; 28_04_2026_effects.md §4.3.3 + §4.7 for the
// production-call-site rationale and the canonical wrapper-nesting
// story; CLAUDE.md §IX.5 for the latency hierarchy this wrapper
// type-fences.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/WaitLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the WaitStrategy enum into the safety:: namespace under
// `WaitStrategy_v`.  No name collision — the wrapper class is
// `Wait`, not `WaitStrategy`.  Naming convention matches
// HotPathTier_v + DetSafeTier_v + Consistency_v + Lifetime_v.
using ::crucible::algebra::lattices::WaitLattice;
using WaitStrategy_v = ::crucible::algebra::lattices::WaitStrategy;

template <WaitStrategy_v Strategy, typename T>
class [[nodiscard]] Wait {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = WaitLattice::At<Strategy>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned strategy — exposed as a static constexpr for callers
    // doing strategy-aware dispatch without instantiating the wrapper.
    static constexpr WaitStrategy_v strategy = Strategy;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // SEMANTIC NOTE: a default-constructed Wait<SpinPause, T> claims
    // its T{} bytes were produced under SpinPause discipline.  For
    // trivially-zero T, vacuously true.  For non-trivial T, the
    // claim becomes meaningful only if the wrapper is constructed
    // in a context that genuinely honors the strategy.
    constexpr Wait() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Wait(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Wait(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — Wait IS COPYABLE within the
    // same strategy pin.
    constexpr Wait(const Wait&)            = default;
    constexpr Wait(Wait&&)                 = default;
    constexpr Wait& operator=(const Wait&) = default;
    constexpr Wait& operator=(Wait&&)      = default;
    ~Wait()                                = default;

    // Equality: compares value bytes within the SAME strategy pin.
    [[nodiscard]] friend constexpr bool operator==(
        Wait const& a, Wait const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(Wait& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Wait& a, Wait& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredStrategy> — static subsumption check ────
    //
    // True iff this wrapper's pinned strategy is at least as strong
    // as RequiredStrategy.  Stronger wait-discipline satisfies
    // weaker requirement.
    template <WaitStrategy_v RequiredStrategy>
    static constexpr bool satisfies = WaitLattice::leq(RequiredStrategy, Strategy);

    // ── relax<WeakerStrategy> — convert to a less-strict strategy ─
    //
    // Returns a Wait<WeakerStrategy, T> carrying the same value
    // bytes.  Allowed iff WeakerStrategy ≤ Strategy in the lattice.
    // Compile error when WeakerStrategy > Strategy — would CLAIM
    // more wait-discipline than the source provides.
    template <WaitStrategy_v WeakerStrategy>
        requires (WaitLattice::leq(WeakerStrategy, Strategy))
    [[nodiscard]] constexpr Wait<WeakerStrategy, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Wait<WeakerStrategy, T>{this->peek()};
    }

    template <WaitStrategy_v WeakerStrategy>
        requires (WaitLattice::leq(WeakerStrategy, Strategy))
    [[nodiscard]] constexpr Wait<WeakerStrategy, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Wait<WeakerStrategy, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace wait {
    template <typename T> using SpinPause   = Wait<WaitStrategy_v::SpinPause,   T>;
    template <typename T> using BoundedSpin = Wait<WaitStrategy_v::BoundedSpin, T>;
    template <typename T> using UmwaitC01   = Wait<WaitStrategy_v::UmwaitC01,   T>;
    template <typename T> using AcquireWait = Wait<WaitStrategy_v::AcquireWait, T>;
    template <typename T> using Park        = Wait<WaitStrategy_v::Park,        T>;
    template <typename T> using Block       = Wait<WaitStrategy_v::Block,       T>;
}  // namespace wait

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::wait_layout {

template <typename T> using SpinW   = Wait<WaitStrategy_v::SpinPause,   T>;
template <typename T> using ParkW   = Wait<WaitStrategy_v::Park,        T>;
template <typename T> using BlockW  = Wait<WaitStrategy_v::Block,       T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinW,  char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinW,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinW,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ParkW,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ParkW,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BlockW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BlockW, double);

}  // namespace detail::wait_layout

static_assert(sizeof(Wait<WaitStrategy_v::SpinPause,   int>)    == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::BoundedSpin, int>)    == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::UmwaitC01,   int>)    == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::AcquireWait, int>)    == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::Park,        int>)    == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::Block,       int>)    == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::SpinPause,   double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::wait_self_test {

using SpinInt    = Wait<WaitStrategy_v::SpinPause,   int>;
using BoundInt   = Wait<WaitStrategy_v::BoundedSpin, int>;
using UmwaitInt  = Wait<WaitStrategy_v::UmwaitC01,   int>;
using FutexInt   = Wait<WaitStrategy_v::AcquireWait, int>;
using ParkInt    = Wait<WaitStrategy_v::Park,        int>;
using BlockInt   = Wait<WaitStrategy_v::Block,       int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr SpinInt w_default{};
static_assert(w_default.peek() == 0);
static_assert(w_default.strategy == WaitStrategy_v::SpinPause);

inline constexpr SpinInt w_explicit{42};
static_assert(w_explicit.peek() == 42);

inline constexpr SpinInt w_in_place{std::in_place, 7};
static_assert(w_in_place.peek() == 7);

// ── Pinned strategy accessor ──────────────────────────────────────
static_assert(SpinInt::strategy   == WaitStrategy_v::SpinPause);
static_assert(BoundInt::strategy  == WaitStrategy_v::BoundedSpin);
static_assert(UmwaitInt::strategy == WaitStrategy_v::UmwaitC01);
static_assert(FutexInt::strategy  == WaitStrategy_v::AcquireWait);
static_assert(ParkInt::strategy   == WaitStrategy_v::Park);
static_assert(BlockInt::strategy  == WaitStrategy_v::Block);

// ── satisfies<RequiredStrategy> — subsumption-up direction ────────
//
// SpinPause satisfies every consumer.
static_assert(SpinInt::satisfies<WaitStrategy_v::SpinPause>);
static_assert(SpinInt::satisfies<WaitStrategy_v::BoundedSpin>);
static_assert(SpinInt::satisfies<WaitStrategy_v::UmwaitC01>);
static_assert(SpinInt::satisfies<WaitStrategy_v::AcquireWait>);
static_assert(SpinInt::satisfies<WaitStrategy_v::Park>);
static_assert(SpinInt::satisfies<WaitStrategy_v::Block>);

// AcquireWait (futex) satisfies weaker-or-equal strategies; FAILS on
// stronger.  THIS IS THE LOAD-BEARING POSITIVE TEST: futex-tier
// values pass the gate of futex-tier or weaker consumers, but FAIL
// the hot-path-only gate (SpinPause / BoundedSpin).
static_assert( FutexInt::satisfies<WaitStrategy_v::AcquireWait>);   // self
static_assert( FutexInt::satisfies<WaitStrategy_v::Park>);          // weaker
static_assert( FutexInt::satisfies<WaitStrategy_v::Block>);
static_assert(!FutexInt::satisfies<WaitStrategy_v::UmwaitC01>);     // stronger fails ✓
static_assert(!FutexInt::satisfies<WaitStrategy_v::BoundedSpin>);
static_assert(!FutexInt::satisfies<WaitStrategy_v::SpinPause>,
    "AcquireWait MUST NOT satisfy SpinPause — this is the load-"
    "bearing rejection that the hot-path waiter discipline depends "
    "on.  If this fires, futex-backed waits could silently flow "
    "into hot-path SPSC ring polls and miss the CLAUDE.md §IX.5 "
    "10-40ns wait floor.");

// Park satisfies Park / Block; FAILS on stronger.
static_assert( ParkInt::satisfies<WaitStrategy_v::Park>);
static_assert( ParkInt::satisfies<WaitStrategy_v::Block>);
static_assert(!ParkInt::satisfies<WaitStrategy_v::AcquireWait>);
static_assert(!ParkInt::satisfies<WaitStrategy_v::SpinPause>);

// Block satisfies only Block.
static_assert( BlockInt::satisfies<WaitStrategy_v::Block>);
static_assert(!BlockInt::satisfies<WaitStrategy_v::Park>);
static_assert(!BlockInt::satisfies<WaitStrategy_v::AcquireWait>);
static_assert(!BlockInt::satisfies<WaitStrategy_v::SpinPause>);

// ── relax<WeakerStrategy> — DOWN-the-lattice conversion ───────────
inline constexpr auto from_spin_to_bound =
    SpinInt{42}.relax<WaitStrategy_v::BoundedSpin>();
static_assert(from_spin_to_bound.peek() == 42);
static_assert(from_spin_to_bound.strategy == WaitStrategy_v::BoundedSpin);

inline constexpr auto from_spin_to_block =
    SpinInt{99}.relax<WaitStrategy_v::Block>();
static_assert(from_spin_to_block.peek() == 99);
static_assert(from_spin_to_block.strategy == WaitStrategy_v::Block);

inline constexpr auto from_umwait_to_park =
    UmwaitInt{7}.relax<WaitStrategy_v::Park>();
static_assert(from_umwait_to_park.peek() == 7);

inline constexpr auto from_futex_to_self =
    FutexInt{8}.relax<WaitStrategy_v::AcquireWait>();   // identity
static_assert(from_futex_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, WaitStrategy_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<SpinInt,   WaitStrategy_v::BoundedSpin>);    // ✓ down
static_assert( can_relax<SpinInt,   WaitStrategy_v::Block>);          // ✓ down (full chain)
static_assert( can_relax<SpinInt,   WaitStrategy_v::SpinPause>);      // ✓ self
static_assert( can_relax<UmwaitInt, WaitStrategy_v::AcquireWait>);    // ✓ down
static_assert( can_relax<UmwaitInt, WaitStrategy_v::UmwaitC01>);      // ✓ self
static_assert(!can_relax<UmwaitInt, WaitStrategy_v::BoundedSpin>,      // ✗ up
    "relax<BoundedSpin> on an UmwaitC01-pinned wrapper MUST be "
    "rejected — claiming a stronger wait-discipline than the source "
    "provides defeats the hot-path admission gate.");
static_assert(!can_relax<UmwaitInt, WaitStrategy_v::SpinPause>);      // ✗ up
static_assert(!can_relax<ParkInt,   WaitStrategy_v::AcquireWait>);    // ✗ up
static_assert(!can_relax<BlockInt,  WaitStrategy_v::Park>);           // ✗ up
// Block reflexivity — chain endpoint admits relax to itself.
static_assert( can_relax<BlockInt,  WaitStrategy_v::Block>);          // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(SpinInt::value_type_name().ends_with("int"));
static_assert(SpinInt::lattice_name()   == "WaitLattice::At<SpinPause>");
static_assert(BoundInt::lattice_name()  == "WaitLattice::At<BoundedSpin>");
static_assert(UmwaitInt::lattice_name() == "WaitLattice::At<UmwaitC01>");
static_assert(FutexInt::lattice_name()  == "WaitLattice::At<AcquireWait>");
static_assert(ParkInt::lattice_name()   == "WaitLattice::At<Park>");
static_assert(BlockInt::lattice_name()  == "WaitLattice::At<Block>");

// ── swap exchanges T values within the same strategy pin ─────────
[[nodiscard]] consteval bool swap_exchanges_within_same_strategy() noexcept {
    SpinInt a{10};
    SpinInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_strategy());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    SpinInt a{10};
    SpinInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    SpinInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-strategy, same-T comparison ─────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    SpinInt a{42};
    SpinInt b{42};
    SpinInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// SFINAE: operator== is only present when T has its own ==.
struct NoEqualityT {
    int v{0};
    NoEqualityT() = default;
    explicit NoEqualityT(int x) : v{x} {}
    NoEqualityT(NoEqualityT&&) = default;
    NoEqualityT& operator=(NoEqualityT&&) = default;
    NoEqualityT(NoEqualityT const&) = delete;
    NoEqualityT& operator=(NoEqualityT const&) = delete;
};

template <typename W>
concept can_equality_compare = requires(W const& a, W const& b) {
    { a == b } -> std::convertible_to<bool>;
};

static_assert( can_equality_compare<SpinInt>);
static_assert(!can_equality_compare<Wait<WaitStrategy_v::SpinPause, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — Wait<SpinPause, NoEqualityT>
// must inherit that deletion.
static_assert(!std::is_copy_constructible_v<Wait<WaitStrategy_v::SpinPause, NoEqualityT>>,
    "Wait<Strategy, T> must transitively inherit T's copy-deletion. "
    "If this fires, NoEqualityT's deleted copy ctor is no longer "
    "visible through the wrapper.");
static_assert(std::is_move_constructible_v<Wait<WaitStrategy_v::SpinPause, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    SpinInt a{99};
    auto b = a.relax<WaitStrategy_v::SpinPause>();
    return b.peek() == 99 && b.strategy == WaitStrategy_v::SpinPause;
}
static_assert(relax_to_self_is_identity());

// ── relax<>() && works on move-only T ─────────────────────────────
//
// Mirrors the DetSafe + HotPath move-only-relax discipline.  The
// relax() && overload moves T through `std::move(impl_).consume()`
// — does NOT require copy_constructible<T>.
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, WaitStrategy_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, WaitStrategy_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using SpinMoveOnly = Wait<WaitStrategy_v::SpinPause, MoveOnlyT>;
static_assert( can_relax_rvalue<SpinMoveOnly, WaitStrategy_v::Park>,
    "relax<>() && MUST work for move-only T.");
static_assert(!can_relax_lvalue<SpinMoveOnly, WaitStrategy_v::Park>,
    "relax<>() const& on move-only T MUST be rejected.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    SpinMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<WaitStrategy_v::Park>();
    return dst.peek().v == 77 && dst.strategy == WaitStrategy_v::Park;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(SpinInt::value_type_name().size() > 0);
static_assert(SpinInt::lattice_name().size() > 0);
static_assert(SpinInt::lattice_name().starts_with("WaitLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(wait::SpinPause<int>::strategy   == WaitStrategy_v::SpinPause);
static_assert(wait::BoundedSpin<int>::strategy == WaitStrategy_v::BoundedSpin);
static_assert(wait::UmwaitC01<int>::strategy   == WaitStrategy_v::UmwaitC01);
static_assert(wait::AcquireWait<int>::strategy == WaitStrategy_v::AcquireWait);
static_assert(wait::Park<int>::strategy        == WaitStrategy_v::Park);
static_assert(wait::Block<int>::strategy       == WaitStrategy_v::Block);

static_assert(std::is_same_v<wait::SpinPause<double>,
                             Wait<WaitStrategy_v::SpinPause, double>>);

// ── Hot-path waiter admission simulation ─────────────────────────
//
// The dispatcher's hot-path admission gate (per 28_04 §6.4 + the
// canonical wrapper-nesting from §4.7) refuses Wait callees whose
// strategy is weaker than SpinPause.  Concrete simulation:
//
//   template <typename T>
//   void hot_path_waiter_site(HotPath<Hot, Wait<SpinPause, T>>);
//
// Below: the concept is_hot_path_waiter_admissible proves that
//   SpinPause-tier values PASS the gate (✓)
//   BoundedSpin-tier values are REJECTED (load-bearing if hot path
//                                         requires _mm_pause-only)
//   AcquireWait / Park / Block are REJECTED (✓ — can't futex on hot path)

template <typename W>
concept is_hot_path_waiter_admissible =
    W::template satisfies<WaitStrategy_v::SpinPause>;

static_assert( is_hot_path_waiter_admissible<SpinInt>,
    "SpinPause-tier value MUST pass the hot-path waiter gate.");
static_assert(!is_hot_path_waiter_admissible<BoundInt>,
    "BoundedSpin-tier value MUST be REJECTED at the strict hot-path "
    "waiter gate (BoundedSpin includes backoff that may exceed the "
    "_mm_pause budget).");
static_assert(!is_hot_path_waiter_admissible<FutexInt>,
    "AcquireWait-tier value MUST be REJECTED — futex (1-5μs) is "
    "banned on hot path per CLAUDE.md §IX.5.");
static_assert(!is_hot_path_waiter_admissible<ParkInt>,
    "Park-tier value MUST be REJECTED at the hot-path waiter gate.");
static_assert(!is_hot_path_waiter_admissible<BlockInt>,
    "Block-tier value MUST be REJECTED at the hot-path waiter gate.");

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Construction paths.
    SpinInt a{};
    SpinInt b{42};
    SpinInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static strategy accessor at runtime.
    if (SpinInt::strategy != WaitStrategy_v::SpinPause) {
        std::abort();
    }

    // peek_mut.
    SpinInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap at runtime.
    SpinInt sx{1};
    SpinInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // relax<WeakerStrategy> — both overloads.
    SpinInt source{77};
    auto relaxed_copy = source.relax<WaitStrategy_v::BoundedSpin>();
    auto relaxed_move = std::move(source).relax<WaitStrategy_v::Park>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = SpinInt::satisfies<WaitStrategy_v::Park>;
    [[maybe_unused]] bool s2 = ParkInt::satisfies<WaitStrategy_v::SpinPause>;

    // operator== — same-strategy.
    SpinInt eq_a{42};
    SpinInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    SpinInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    wait::SpinPause<int>   alias_spin{123};
    wait::Park<int>        alias_park{456};
    wait::Block<int>       alias_block{789};
    [[maybe_unused]] auto sv = alias_spin.peek();
    [[maybe_unused]] auto pv = alias_park.peek();
    [[maybe_unused]] auto bv = alias_block.peek();

    // Hot-path waiter admission simulation at runtime.
    [[maybe_unused]] bool can_spin_pass  = is_hot_path_waiter_admissible<SpinInt>;
    [[maybe_unused]] bool can_futex_pass = is_hot_path_waiter_admissible<FutexInt>;
    [[maybe_unused]] bool can_block_pass = is_hot_path_waiter_admissible<BlockInt>;
}

}  // namespace detail::wait_self_test

}  // namespace crucible::safety
