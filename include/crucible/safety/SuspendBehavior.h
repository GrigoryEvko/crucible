#pragma once

// Records whether the clock behind a value keeps advancing across a
// suspend and resume cycle.  CLOCK_MONOTONIC pauses.  CLOCK_BOOTTIME
// keeps ticking.
//
// satisfies names a floor, not a ceiling: a witness satisfies a
// requirement when its own behavior sits at or above the required one
// on the chain Unknown, PausesOnSuspend, KeepsTicking.
//
// Anything timing a deadline has to require KeepsTicking.  A clock that
// pauses reports a long suspend as no elapsed time, so the deadline
// looks unbreached on the far side of the resume.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

#include <concepts>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::SuspendBehaviorLattice;
using SuspendBehavior_v = ::crucible::algebra::lattices::SuspendBehavior;

template <SuspendBehavior_v Behavior, typename T>
class [[nodiscard]] SuspendBehavior {
public:
    using value_type = T;
    using lattice_type = SuspendBehaviorLattice::template At<Behavior>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr SuspendBehavior_v behavior = Behavior;

private:
    graded_type impl_;

public:
    constexpr SuspendBehavior() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit SuspendBehavior(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit SuspendBehavior(std::in_place_t,
                                       Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr SuspendBehavior(const SuspendBehavior&) = default;
    constexpr SuspendBehavior(SuspendBehavior&&) = default;
    constexpr SuspendBehavior& operator=(const SuspendBehavior&) = default;
    constexpr SuspendBehavior& operator=(SuspendBehavior&&) = default;
    ~SuspendBehavior() = default;

    [[nodiscard]] friend constexpr bool operator==(SuspendBehavior const& a,
                                                   SuspendBehavior const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(SuspendBehavior& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(SuspendBehavior& a, SuspendBehavior& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <SuspendBehavior_v Required>
    static constexpr bool satisfies = SuspendBehaviorLattice::leq(Required, Behavior);
};

template <SuspendBehavior_v Behavior, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr SuspendBehavior<Behavior, T>
mint_suspend_behavior(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return SuspendBehavior<Behavior, T>{std::in_place, std::forward<Args>(args)...};
}

// The name avoids a clash with the lattice's own alias namespace.
namespace suspend_witness {
template <typename T>
using Unknown = SuspendBehavior<SuspendBehavior_v::Unknown, T>;
template <typename T>
using PausesOnSuspend = SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, T>;
template <typename T>
using KeepsTicking = SuspendBehavior<SuspendBehavior_v::KeepsTicking, T>;
}  // namespace suspend_witness

namespace detail::suspend_behavior_layout {

template <typename T>
using PausesSb = SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, T>;
template <typename T>
using KeepsSb = SuspendBehavior<SuspendBehavior_v::KeepsTicking, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(PausesSb, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(KeepsSb, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(KeepsSb, unsigned long long);

}  // namespace detail::suspend_behavior_layout

static_assert(sizeof(SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, int>) == sizeof(int));
static_assert(sizeof(SuspendBehavior<SuspendBehavior_v::KeepsTicking, unsigned long long>)
              == sizeof(unsigned long long));

namespace detail::suspend_behavior_self_test {

using PausesU64 = SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, unsigned long long>;
using KeepsU64 = SuspendBehavior<SuspendBehavior_v::KeepsTicking, unsigned long long>;
using UnkU64 = SuspendBehavior<SuspendBehavior_v::Unknown, unsigned long long>;

inline constexpr KeepsU64 k_default{};
static_assert(k_default.peek() == 0);
static_assert(KeepsU64::behavior == SuspendBehavior_v::KeepsTicking);

inline constexpr KeepsU64 k_explicit{42};
static_assert(k_explicit.peek() == 42);

inline constexpr PausesU64 p_in_place{std::in_place, 7};
static_assert(p_in_place.peek() == 7);

static_assert(KeepsU64::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(KeepsU64::satisfies<SuspendBehavior_v::KeepsTicking>,
              "a KeepsTicking (CLOCK_BOOTTIME) witness MUST satisfy a "
              "KeepsTicking deadline-watchdog requirement.");
static_assert(!PausesU64::satisfies<SuspendBehavior_v::KeepsTicking>,
              "a PausesOnSuspend (CLOCK_MONOTONIC) witness MUST NOT satisfy "
              "a KeepsTicking requirement — the watchdog needs CLOCK_BOOTTIME.");
static_assert(!UnkU64::satisfies<SuspendBehavior_v::KeepsTicking>);
static_assert(PausesU64::satisfies<SuspendBehavior_v::Unknown>);
static_assert(KeepsU64::satisfies<SuspendBehavior_v::PausesOnSuspend>,
              "KeepsTicking ⊒ PausesOnSuspend — a suspend-inclusive clock also meets a "
              "pause-tolerating requirement.");
static_assert(!PausesU64::satisfies<SuspendBehavior_v::KeepsTicking>);

static_assert(!std::is_same_v<PausesU64, KeepsU64>);
static_assert(!std::is_convertible_v<PausesU64, KeepsU64>);

static_assert(KeepsU64::lattice_name() == "SuspendBehaviorLattice::At<KeepsTicking>");
static_assert(PausesU64::lattice_name() == "SuspendBehaviorLattice::At<PausesOnSuspend>");
static_assert(KeepsU64::value_type_name().find("long") != std::string_view::npos);

[[nodiscard]] consteval bool swap_exchanges_within_same_behavior() noexcept {
    KeepsU64 a{10};
    KeepsU64 b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_behavior());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    KeepsU64 a{42};
    KeepsU64 b{42};
    KeepsU64 c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

inline constexpr auto minted = mint_suspend_behavior<SuspendBehavior_v::KeepsTicking, unsigned long long>(99);
static_assert(minted.peek() == 99 && minted.behavior == SuspendBehavior_v::KeepsTicking);

static_assert(std::is_same_v<suspend_witness::KeepsTicking<unsigned long long>, KeepsU64>);
static_assert(std::is_same_v<suspend_witness::PausesOnSuspend<unsigned long long>, PausesU64>);

template <typename Witness>
concept survives_suspend = Witness::template satisfies<SuspendBehavior_v::KeepsTicking>;

static_assert(survives_suspend<KeepsU64>, "A CLOCK_BOOTTIME witness MUST pass the deadline-watchdog gate.");
static_assert(!survives_suspend<PausesU64>,
              "A CLOCK_MONOTONIC witness MUST be rejected at the deadline-watchdog gate.");
static_assert(survives_suspend<suspend_witness::KeepsTicking<int>>,
              "a freshness check across a suspend legitimately uses a "
              "KeepsTicking witness — this MUST compile.");

inline void runtime_smoke_test() {
    unsigned long long seed = 21;
    KeepsU64 k{seed * 2};
    if (k.peek() != 42) std::abort();
    k.peek_mut() = 9;
    if (k.peek() != 9) std::abort();

    auto m = mint_suspend_behavior<SuspendBehavior_v::PausesOnSuspend, unsigned long long>(seed);
    if (std::move(m).consume() != 21) std::abort();

    KeepsU64 a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool g1 = KeepsU64::satisfies<SuspendBehavior_v::KeepsTicking>;
    [[maybe_unused]] bool g2 = PausesU64::satisfies<SuspendBehavior_v::KeepsTicking>;
    if (!g1 || g2) std::abort();

    suspend_witness::Unknown<unsigned long long> u{123};
    if (u.peek() != 123) std::abort();
}

}  // namespace detail::suspend_behavior_self_test

}  // namespace crucible::safety
