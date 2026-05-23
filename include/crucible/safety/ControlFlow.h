#pragma once

// ── crucible::safety::ControlFlowPinned<ControlFlow Tier, typename T> ─
//
// FIXY-V-242 (1/5): value-level Graded carrier for the V-239
// ControlFlow axis (Pure ⊏ AbortOnly ⊏ ThrowOnly ⊏ MayLongjmp ⊏
// MaySignal).  Pins a function-result's non-local-control-flow-escape
// capability into the type.
//
//   Substrate: Graded<ModalityKind::Absolute, ControlFlowLattice::At<Tier>, T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//              empty; sizeof(ControlFlowPinned<Tier, T>) == sizeof(T)).
//
// ── Why Modality::Absolute ──────────────────────────────────────────
//
// A control-flow-escape tier is a STATIC property of how a value was
// produced ("this result came from a function that may longjmp"), not
// a content-derived invariant.  Mutating the inner T cannot change the
// escape capability — Absolute admits peek_mut/swap unconditionally
// (mirrors FpModePinned; contrast Witness's Comonad counit, which has
// no analogue here — erasing the tier is not a meaningful operation).
//
// ── Ceiling semantics — the mirror image of Witness ─────────────────
//
// ControlFlow is a CAPABILITY CEILING axis, not a proof FLOOR.  Bottom
// (Pure) is the SAFEST tier; top (MaySignal) the most dangerous.  A
// consumer imposes a ceiling C and admits a value iff its tier ⊑ C:
//
//   satisfies<C> := ControlFlowLattice::leq(Tier, C)
//
// e.g. Forge hot-path admission imposes ceiling Pure; only a
// ControlFlowPinned<Pure, T> satisfies it.  This is the REVERSE of
// Witness::satisfies (which is leq(Required, Tier) — a floor).
//
// The sound conversion is `widen<Higher>()` (UP the chain): a Pure
// value may be conservatively re-declared as MaySignal-capable
// (over-approximating capability is always safe).  The converse —
// claiming a value is SAFER than it is — is forbidden; no `tighten()`
// exists.  Compile error when Higher < Tier.  This is the REVERSE of
// Witness::relax (which goes DOWN).
//
// ── §XXI Universal Mint + HS14 ──────────────────────────────────────
//
// `mint_control_flow<Tier, T>(args...)` — token mint (no Ctx),
// requires std::is_constructible_v<T, Args...>.  Two HS14 neg-compile
// fixtures at test/safety_neg/ witness the gates fire across distinct
// mismatch classes:
//   1. widen-to-lower — neg_control_flow_widen_to_lower.cpp
//   2. mint-wrong-arg — neg_control_flow_mint_wrong_arg.cpp
//
// See V-239 (ControlFlowLattice.h) for the lattice; V-243 for the
// CollisionCatalog cross-axis rules; V-244 (fixy/grant/Ctrl.h) for the
// grant tags that route onto this axis.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ControlFlowLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::ControlFlow;
using ::crucible::algebra::lattices::ControlFlowLattice;

template <ControlFlow Tier, typename T>
class [[nodiscard]] ControlFlowPinned {
public:
    using value_type   = T;
    using lattice_type = ControlFlowLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;
    static constexpr ControlFlow tier = Tier;

private:
    graded_type impl_;

public:
    constexpr ControlFlowPinned() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit ControlFlowPinned(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ControlFlowPinned(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr ControlFlowPinned(const ControlFlowPinned&)            = default;
    constexpr ControlFlowPinned(ControlFlowPinned&&)                 = default;
    constexpr ControlFlowPinned& operator=(const ControlFlowPinned&) = default;
    constexpr ControlFlowPinned& operator=(ControlFlowPinned&&)      = default;
    ~ControlFlowPinned()                                             = default;

    [[nodiscard]] friend constexpr bool operator==(
        ControlFlowPinned const& a, ControlFlowPinned const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return std::move(impl_).consume(); }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(ControlFlowPinned& other)
        noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(ControlFlowPinned& a, ControlFlowPinned& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── satisfies<Ceiling> — ceiling-direction admission check ─────────
    // True iff this tier is WITHIN the consumer's ceiling: leq(Tier, C).
    template <ControlFlow Ceiling>
    static constexpr bool satisfies = ControlFlowLattice::leq(Tier, Ceiling);

    // ── widen<Higher> — UP-the-chain (over-approximate capability) ─────
    // Sound: a safer value may be conservatively declared more-capable.
    // Compile error when Higher < Tier (cannot claim safer-than-real).
    template <ControlFlow Higher>
        requires (ControlFlowLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr ControlFlowPinned<Higher, T> widen() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return ControlFlowPinned<Higher, T>{this->peek()}; }

    template <ControlFlow Higher>
        requires (ControlFlowLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr ControlFlowPinned<Higher, T> widen() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return ControlFlowPinned<Higher, T>{std::move(impl_).consume()}; }
};

// ── §XXI Universal Mint factory (token mint) ────────────────────────
template <ControlFlow Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr ControlFlowPinned<Tier, T> mint_control_flow(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return ControlFlowPinned<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Per-tier convenience aliases ────────────────────────────────────
namespace control_flow_pin {
    template <typename T> using Pure       = ControlFlowPinned<ControlFlow::Pure,       T>;
    template <typename T> using AbortOnly  = ControlFlowPinned<ControlFlow::AbortOnly,  T>;
    template <typename T> using ThrowOnly  = ControlFlowPinned<ControlFlow::ThrowOnly,  T>;
    template <typename T> using MayLongjmp = ControlFlowPinned<ControlFlow::MayLongjmp, T>;
    template <typename T> using MaySignal  = ControlFlowPinned<ControlFlow::MaySignal,  T>;
}  // namespace control_flow_pin

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
static_assert(sizeof(ControlFlowPinned<ControlFlow::Pure,      int>)    == sizeof(int));
static_assert(sizeof(ControlFlowPinned<ControlFlow::MaySignal, int>)    == sizeof(int));
static_assert(sizeof(ControlFlowPinned<ControlFlow::ThrowOnly, double>) == sizeof(double));
static_assert(sizeof(ControlFlowPinned<ControlFlow::Pure,      char>)   == sizeof(char));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::control_flow_pinned_self_test {

using PureInt   = ControlFlowPinned<ControlFlow::Pure,      int>;
using SignalInt = ControlFlowPinned<ControlFlow::MaySignal, int>;

inline constexpr PureInt cf_default{};
static_assert(cf_default.peek() == 0);
static_assert(PureInt::tier == ControlFlow::Pure);
static_assert(SignalInt::tier == ControlFlow::MaySignal);
static_assert(PureInt::modality == ::crucible::algebra::ModalityKind::Absolute);

// satisfies — ceiling direction.  Pure satisfies every ceiling; MaySignal
// satisfies only the MaySignal ceiling.
static_assert(PureInt::satisfies<ControlFlow::Pure>);
static_assert(PureInt::satisfies<ControlFlow::MaySignal>);
static_assert( SignalInt::satisfies<ControlFlow::MaySignal>);
static_assert(!SignalInt::satisfies<ControlFlow::Pure>);
static_assert(!SignalInt::satisfies<ControlFlow::ThrowOnly>);

// widen — UP the chain only.  Pure widens to MaySignal; the value is
// preserved, the declared capability is over-approximated.
inline constexpr auto widened = PureInt{42}.widen<ControlFlow::MaySignal>();
static_assert(widened.peek() == 42);
static_assert(widened.tier == ControlFlow::MaySignal);
inline constexpr auto identity_widen = PureInt{7}.widen<ControlFlow::Pure>();
static_assert(identity_widen.tier == ControlFlow::Pure);

// mint factory + per-tier aliases round-trip.
inline constexpr auto minted = mint_control_flow<ControlFlow::AbortOnly, int>(99);
static_assert(minted.peek() == 99 && minted.tier == ControlFlow::AbortOnly);
static_assert(std::is_same_v<control_flow_pin::Pure<int>, PureInt>);
static_assert(std::is_same_v<control_flow_pin::MaySignal<int>, SignalInt>);

// Cross-tier type distinctness (federation-slot disambiguation).
static_assert(!std::is_same_v<PureInt, SignalInt>);

// Copyable (capability metadata, not a classified channel).
static_assert(std::is_copy_constructible_v<PureInt>);
static_assert(std::is_move_constructible_v<PureInt>);

// Runtime smoke (non-constant operands).
inline void runtime_smoke_test() {
    int seed = 11;
    PureInt p{seed * 2};
    if (p.peek() != 22) std::abort();
    p.peek_mut() = 5;
    if (p.peek() != 5) std::abort();
    auto w = PureInt{seed}.widen<ControlFlow::ThrowOnly>();
    if (w.peek() != 11 || w.tier != ControlFlow::ThrowOnly) std::abort();
    auto m = mint_control_flow<ControlFlow::MaySignal, int>(seed);
    if (std::move(m).consume() != 11) std::abort();
    PureInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::control_flow_pinned_self_test

}  // namespace crucible::safety
