#pragma once

// ── crucible::safety::BarrierGuarded<BarrierStrength_v Tier, typename T> ─
//
// FIXY-V-255 (Agent 11 §3.7): value-level Graded carrier for the V-253
// BarrierStrength axis (None ⊑ CompilerBarrier ⊑ AcquireLoad ⊑
// ReleaseStore ⊑ AcqRel ⊑ SeqCst ⊑ FullFence — V-252
// BarrierStrengthLattice).  Pins, at the TYPE level, the memory-fence
// ordering a value was published under, so a consumer that REQUIRES a
// minimum ordering can reject a value guarded by a weaker barrier at
// compile time.
//
//   Substrate: Graded<ModalityKind::Absolute, BarrierStrengthLattice::At<Tier>, T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//              empty, sizeof(BarrierGuarded<Tier, T>) == sizeof(T) at -O3).
//
// ── FLOOR semantics — the DUAL of Hw's ceiling ─────────────────────
//
// Hw<Tier, T> (V-254) uses CEILING subsumption: a value flows into a
// consumer whose admitted ceiling is at-or-ABOVE the value's tier
// (`leq(Tier, Ceiling)`), and `widen<Higher>()` moves UP the chain.
// BarrierGuarded inverts both, because a STRONGER fence subsumes a
// WEAKER requirement: a value published under SeqCst genuinely provides
// AcqRel ordering, so it satisfies any consumer that needs only AcqRel.
//
//   satisfies<Required> := BarrierStrengthLattice::leq(Required, Tier)
//        (the published fence Tier must be at-or-ABOVE the consumer's
//         required FLOOR — Required ⊑ Tier)
//   weaken<Lower>()     — DOWN the chain only (Lower ⊑ Tier)
//
// A consumer that requires a floor F admits any BarrierGuarded<Tier, T>
// with F ⊑ Tier.  A `satisfies<AcqRel>` floor admits
// BarrierGuarded<AcqRel>, <SeqCst>, <FullFence> and rejects
// BarrierGuarded<AcquireLoad>, <CompilerBarrier>, <None> — the
// consumer-floor-not-met rejection that neg_barrier_consumer_too_weak.cpp
// pins.  `weaken<Lower>()` is sound because relabelling a FullFence value
// as merely AcqRel claims LESS than the value provides; `strengthen` UP
// (claiming a barrier stronger than was issued) is a compile error —
// neg_barrier_strengthen_up.cpp.
//
// ── §XVI canonical wrapper-nesting position + source composition ───
//
// Memory fences are architecture-specific (mfence on x86, DMB ISH on
// ARM), so a BarrierGuarded value is naturally further pinned to the
// arch that issued the fence.  That composition is realized by wrapping
// BarrierGuarded in the existing Tagged<T, Source> machinery — e.g.
// `Tagged<BarrierGuarded<AcqRel, T>, source::ArchPinned<X86>>` once the
// V-261 source::ArchPinned<Arch> tag + cross-arch composition gate land.
// Until then the composition is demonstrated against an existing source
// tag in the sentinel TU; the row_hash is order-sensitive so the future
// ArchPinned tag slots into the identical Tagged nest with zero churn.
//
// The row_hash_contribution<BarrierGuarded<Tier, Inner>> federation-cache
// discriminator (salt 0x2D) ships in safety/diag/RowHashFold.h, exactly
// like every sister wrapper — the row_hash key is the WRAPPER, never the
// lattice At<>.
//
//   Axiom coverage:
//     TypeSafe — BarrierStrength_v is a strong scoped enum; cross-tier
//                mixing requires std::to_underlying and surfaces at the
//                call site.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//     ThreadSafe — BarrierGuarded is the type-level WITNESS of the
//                publication fence; it does not itself emit a fence, but
//                a consumer can statically require one was issued.
//   Runtime cost: sizeof(BarrierGuarded<Tier, T>) == sizeof(T); verified
//     by CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// §XXI: `mint_barrier_guarded<Tier, T>(args...)`.  HS14 neg fixtures:
// neg_barrier_consumer_too_weak.cpp + neg_barrier_strengthen_up.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/BarrierStrengthLattice.h>

#include <concepts>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::BarrierStrengthLattice;
using BarrierStrength_v = ::crucible::algebra::lattices::BarrierStrength;

template <BarrierStrength_v Tier, typename T>
class [[nodiscard]] BarrierGuarded {
public:
    // ── Public type aliases (GradedWrapper uniform surface) ─────────
    using value_type   = T;
    using lattice_type = BarrierStrengthLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned publication fence — exposed for callers doing
    // strength-aware dispatch without instantiating the wrapper.
    static constexpr BarrierStrength_v tier = Tier;

private:
    graded_type impl_;

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr BarrierGuarded() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit BarrierGuarded(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit BarrierGuarded(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr BarrierGuarded(const BarrierGuarded&)            = default;
    constexpr BarrierGuarded(BarrierGuarded&&)                 = default;
    constexpr BarrierGuarded& operator=(const BarrierGuarded&) = default;
    constexpr BarrierGuarded& operator=(BarrierGuarded&&)      = default;
    ~BarrierGuarded()                                          = default;

    // Equality: compares value bytes within the SAME fence pin.
    // Cross-tier comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        BarrierGuarded const& a, BarrierGuarded const& b)
        noexcept(noexcept(a.peek() == b.peek()))
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

    // ── Read-only / mutable access ──────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return std::move(impl_).consume(); }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(BarrierGuarded& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }
    friend constexpr void swap(BarrierGuarded& a, BarrierGuarded& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── satisfies<Required> — FLOOR subsumption ────────────────────
    //
    // True iff this wrapper's published fence is at-or-ABOVE the
    // consumer's required floor.  A `satisfies<AcqRel>` floor admits
    // BarrierGuarded<AcqRel>, <SeqCst>, <FullFence> and rejects
    // BarrierGuarded<AcquireLoad>, <CompilerBarrier>, <None>.
    template <BarrierStrength_v Required>
    static constexpr bool satisfies = BarrierStrengthLattice::leq(Required, Tier);

    // ── weaken<Lower>() — claim a WEAKER fence than was issued ───────
    //
    // Returns a BarrierGuarded<Lower, T> carrying the same value bytes.
    // Allowed iff Lower ⊑ Tier (DOWN the chain only).  Strengthening UP
    // is a compile error — it would CLAIM a stronger fence than the value
    // was actually published under (e.g. relabel an AcquireLoad value as
    // SeqCst), fooling a consumer that requires the stronger ordering.
    template <BarrierStrength_v Lower>
        requires (BarrierStrengthLattice::leq(Lower, Tier))
    [[nodiscard]] constexpr BarrierGuarded<Lower, T> weaken() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return BarrierGuarded<Lower, T>{this->peek()}; }

    template <BarrierStrength_v Lower>
        requires (BarrierStrengthLattice::leq(Lower, Tier))
    [[nodiscard]] constexpr BarrierGuarded<Lower, T> weaken() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return BarrierGuarded<Lower, T>{std::move(impl_).consume()}; }
};

// ── §XXI mint factory ───────────────────────────────────────────────
template <BarrierStrength_v Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr BarrierGuarded<Tier, T> mint_barrier_guarded(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return BarrierGuarded<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases ─────────────────────────────────────────────
namespace barrier_pin {
    template <typename T> using None            = BarrierGuarded<BarrierStrength_v::None,            T>;
    template <typename T> using CompilerBarrier = BarrierGuarded<BarrierStrength_v::CompilerBarrier, T>;
    template <typename T> using AcquireLoad     = BarrierGuarded<BarrierStrength_v::AcquireLoad,     T>;
    template <typename T> using ReleaseStore    = BarrierGuarded<BarrierStrength_v::ReleaseStore,    T>;
    template <typename T> using AcqRel          = BarrierGuarded<BarrierStrength_v::AcqRel,          T>;
    template <typename T> using SeqCst          = BarrierGuarded<BarrierStrength_v::SeqCst,          T>;
    template <typename T> using FullFence       = BarrierGuarded<BarrierStrength_v::FullFence,       T>;
}  // namespace barrier_pin

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
namespace detail::barrier_guarded_layout {

template <typename T> using NoneBg      = BarrierGuarded<BarrierStrength_v::None,      T>;
template <typename T> using AcqRelBg    = BarrierGuarded<BarrierStrength_v::AcqRel,    T>;
template <typename T> using SeqCstBg     = BarrierGuarded<BarrierStrength_v::SeqCst,    T>;
template <typename T> using FullFenceBg = BarrierGuarded<BarrierStrength_v::FullFence, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneBg,      char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneBg,      int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelBg,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelBg,    double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstBg,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FullFenceBg, int);

}  // namespace detail::barrier_guarded_layout

static_assert(sizeof(BarrierGuarded<BarrierStrength_v::None,            int>)    == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::CompilerBarrier, int>)    == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::AcquireLoad,     int>)    == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::ReleaseStore,    int>)    == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::AcqRel,          int>)    == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::SeqCst,          int>)    == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::FullFence,       int>)    == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::AcqRel,          double>) == sizeof(double));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::None,            char>)   == sizeof(char));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::barrier_guarded_self_test {

using NoneInt    = BarrierGuarded<BarrierStrength_v::None,            int>;
using CompInt    = BarrierGuarded<BarrierStrength_v::CompilerBarrier, int>;
using AcqInt     = BarrierGuarded<BarrierStrength_v::AcquireLoad,     int>;
using AcqRelInt  = BarrierGuarded<BarrierStrength_v::AcqRel,          int>;
using SeqCstInt  = BarrierGuarded<BarrierStrength_v::SeqCst,          int>;
using FullInt    = BarrierGuarded<BarrierStrength_v::FullFence,       int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr AcqRelInt a_default{};
static_assert(a_default.peek() == 0);
static_assert(AcqRelInt::tier == BarrierStrength_v::AcqRel);

inline constexpr AcqRelInt a_explicit{42};
static_assert(a_explicit.peek() == 42);

inline constexpr AcqRelInt a_in_place{std::in_place, 7};
static_assert(a_in_place.peek() == 7);

static_assert(NoneInt::tier == BarrierStrength_v::None);
static_assert(FullInt::tier == BarrierStrength_v::FullFence);
static_assert(NoneInt::modality == ::crucible::algebra::ModalityKind::Absolute);

// ── satisfies<Required> — FLOOR subsumption ────────────────────────
//
// FullFence satisfies every floor (it is the top — strongest fence).
static_assert(FullInt::satisfies<BarrierStrength_v::None>);
static_assert(FullInt::satisfies<BarrierStrength_v::AcqRel>);
static_assert(FullInt::satisfies<BarrierStrength_v::SeqCst>);
static_assert(FullInt::satisfies<BarrierStrength_v::FullFence>);

// SeqCst flows into an AcqRel floor — THE LOAD-BEARING SUBSUMPTION
// (a SeqCst publication genuinely provides AcqRel ordering).
static_assert(SeqCstInt::satisfies<BarrierStrength_v::SeqCst>);
static_assert(SeqCstInt::satisfies<BarrierStrength_v::AcqRel>,
    "BarrierGuarded<SeqCst>::satisfies<AcqRel> MUST be TRUE — a SeqCst "
    "publication subsumes an AcqRel floor (AcqRel ⊑ SeqCst).  This is "
    "the subsumption HS14 fixture.");
static_assert(!SeqCstInt::satisfies<BarrierStrength_v::FullFence>,
    "BarrierGuarded<SeqCst>::satisfies<FullFence> MUST be FALSE — a "
    "SeqCst-ordered value does NOT provide a standalone full fence.");

// None satisfies ONLY the None floor (it is the bottom — no barrier).
static_assert( NoneInt::satisfies<BarrierStrength_v::None>);
static_assert(!NoneInt::satisfies<BarrierStrength_v::CompilerBarrier>);
static_assert(!NoneInt::satisfies<BarrierStrength_v::AcqRel>);

// AcquireLoad does NOT meet an AcqRel floor — the consumer-too-weak case.
static_assert(!AcqInt::satisfies<BarrierStrength_v::AcqRel>,
    "BarrierGuarded<AcquireLoad>::satisfies<AcqRel> MUST be FALSE — an "
    "acquire-only value lacks the release half an AcqRel floor needs.  "
    "This is the consumer-floor-not-met rejection that "
    "neg_barrier_consumer_too_weak.cpp pins at a real gate.");

// ── weaken<Lower>() — DOWN-the-chain conversion ────────────────────
inline constexpr auto full_to_acqrel = FullInt{42}.weaken<BarrierStrength_v::AcqRel>();
static_assert(full_to_acqrel.peek() == 42 && full_to_acqrel.tier == BarrierStrength_v::AcqRel);

inline constexpr auto seqcst_to_none = SeqCstInt{9}.weaken<BarrierStrength_v::None>();
static_assert(seqcst_to_none.peek() == 9 && seqcst_to_none.tier == BarrierStrength_v::None);

inline constexpr auto acqrel_reflexive = AcqRelInt{55}.weaken<BarrierStrength_v::AcqRel>();
static_assert(acqrel_reflexive.peek() == 55);

// ── weaken SFINAE detector — chain-direction check ─────────────────
template <typename W, BarrierStrength_v Target>
concept can_weaken = requires(W w) { { std::move(w).template weaken<Target>() }; };

static_assert( can_weaken<FullInt,   BarrierStrength_v::None>);
static_assert( can_weaken<SeqCstInt, BarrierStrength_v::AcqRel>);
static_assert( can_weaken<AcqRelInt, BarrierStrength_v::AcqRel>);
// Strengthen UP the chain REJECTED — the load-bearing negative.
static_assert(!can_weaken<AcqInt,    BarrierStrength_v::SeqCst>,
    "weaken<SeqCst> on a BarrierGuarded<AcquireLoad> wrapper MUST be "
    "REJECTED — strengthening UP would claim a fence stronger than was "
    "issued, fooling a consumer that requires SeqCst.  See "
    "neg_barrier_strengthen_up.cpp.");
static_assert(!can_weaken<NoneInt,   BarrierStrength_v::CompilerBarrier>);
static_assert(!can_weaken<CompInt,   BarrierStrength_v::AcqRel>);

// ── Diagnostic forwarders ──────────────────────────────────────────
static_assert(AcqRelInt::value_type_name().ends_with("int"));
static_assert(AcqRelInt::lattice_name() == "BarrierStrengthLattice::At<AcqRel>");
static_assert(SeqCstInt::lattice_name() == "BarrierStrengthLattice::At<SeqCst>");
static_assert(FullInt::lattice_name()   == "BarrierStrengthLattice::At<FullFence>");

// ── swap / peek_mut / operator== ───────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    AcqRelInt a{10}; AcqRelInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    AcqRelInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    AcqRelInt a{42}; AcqRelInt b{42}; AcqRelInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// ── Convenience aliases resolve correctly ──────────────────────────
static_assert(std::is_same_v<barrier_pin::AcqRel<int>, AcqRelInt>);
static_assert(std::is_same_v<barrier_pin::FullFence<int>, FullInt>);
static_assert(barrier_pin::SeqCst<double>::tier == BarrierStrength_v::SeqCst);
static_assert(!std::is_same_v<AcqRelInt, SeqCstInt>);
static_assert(std::is_copy_constructible_v<AcqRelInt>);

// ── mint_barrier_guarded factory ───────────────────────────────────
inline constexpr auto minted = mint_barrier_guarded<BarrierStrength_v::SeqCst, int>(99);
static_assert(minted.peek() == 99 && minted.tier == BarrierStrength_v::SeqCst);

// ── Publication-fence admission simulation ─────────────────────────
//
// Production: a consumer that reads a cross-thread-published value
// requires at-least-acquire ordering; it admits only BarrierGuarded
// values whose fence is ⊒ AcquireLoad.
template <typename W>
concept needs_acquire_floor = W::template satisfies<BarrierStrength_v::AcquireLoad>;

static_assert( needs_acquire_floor<AcqInt>,
    "An acquire-published value MUST pass an acquire-floor gate.");
static_assert( needs_acquire_floor<SeqCstInt>,
    "A SeqCst-published value MUST pass an acquire-floor gate "
    "(AcquireLoad ⊑ SeqCst).");
static_assert(!needs_acquire_floor<NoneInt>,
    "A no-barrier value MUST be REJECTED at an acquire-floor gate — it "
    "carries no cross-thread ordering guarantee.");
static_assert(!needs_acquire_floor<CompInt>,
    "A compiler-barrier-only value MUST be REJECTED at an acquire-floor "
    "gate — an optimizer barrier issues no hardware ordering.");

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: pure
// static_asserts can mask consteval/SFINAE/inline-body bugs; runtime
// ops with non-constant arguments catch them.
inline void runtime_smoke_test() {
    int seed = 21;
    AcqRelInt n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto w = FullInt{seed}.weaken<BarrierStrength_v::AcqRel>();
    if (w.peek() != 21 || w.tier != BarrierStrength_v::AcqRel) std::abort();

    auto m = mint_barrier_guarded<BarrierStrength_v::SeqCst, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    AcqRelInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool s1 = SeqCstInt::satisfies<BarrierStrength_v::AcqRel>;
    [[maybe_unused]] bool s2 = AcqInt::satisfies<BarrierStrength_v::AcqRel>;
    if (!s1 || s2) std::abort();

    // Convenience-alias instantiation.
    barrier_pin::None<int>      alias_none{0};
    barrier_pin::FullFence<int> alias_full{456};
    if (alias_none.peek() != 0 || alias_full.peek() != 456) std::abort();
}

}  // namespace detail::barrier_guarded_self_test

}  // namespace crucible::safety
