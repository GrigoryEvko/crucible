#pragma once

// ── crucible::safety::StdioPinned<Stdio Tier, typename T> ────────────
//
// FIXY-V-242 (5/5): value-level Graded carrier for the V-241 Stdio axis
// (NoStdio ⊏ BufferedWrite ⊏ UnbufferedWrite ⊏ InteractiveRead).  Pins
// a function's console-I/O surface into the type.
//
//   Substrate: Graded<ModalityKind::Absolute, StdioLattice::At<Tier>, T>
//   Regime:    1 (zero-cost EBO collapse — sizeof == sizeof(T)).
//
// Absolute modality.  CAPABILITY-CEILING semantics: bottom (NoStdio) is
// the safest (no console I/O — the only tier admissible on the hot path
// per CLAUDE.md §XII); top (InteractiveRead) blocks unboundedly on
// interactive input.
//
//   satisfies<C> := StdioLattice::leq(Tier, C)
//   widen<Higher>()                                  — UP the chain only
//
// Forge hot-path admission imposes ceiling NoStdio, turning the §XII
// "no stdio on hot path" prose rule into a type-level gate.
//
// §XXI: `mint_stdio<Tier, T>(args...)`.  HS14 neg fixtures:
// neg_stdio_widen_to_lower.cpp + neg_stdio_mint_wrong_arg.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/StdioLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::Stdio;
using ::crucible::algebra::lattices::StdioLattice;

template <Stdio Tier, typename T>
class [[nodiscard]] StdioPinned {
public:
    using value_type   = T;
    using lattice_type = StdioLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;
    static constexpr Stdio tier = Tier;

private:
    graded_type impl_;

public:
    constexpr StdioPinned() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit StdioPinned(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit StdioPinned(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr StdioPinned(const StdioPinned&)            = default;
    constexpr StdioPinned(StdioPinned&&)                 = default;
    constexpr StdioPinned& operator=(const StdioPinned&) = default;
    constexpr StdioPinned& operator=(StdioPinned&&)      = default;
    ~StdioPinned()                                       = default;

    [[nodiscard]] friend constexpr bool operator==(
        StdioPinned const& a, StdioPinned const& b) noexcept(
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

    constexpr void swap(StdioPinned& other)
        noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(StdioPinned& a, StdioPinned& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <Stdio Ceiling>
    static constexpr bool satisfies = StdioLattice::leq(Tier, Ceiling);

    template <Stdio Higher>
        requires (StdioLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr StdioPinned<Higher, T> widen() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return StdioPinned<Higher, T>{this->peek()}; }

    template <Stdio Higher>
        requires (StdioLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr StdioPinned<Higher, T> widen() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return StdioPinned<Higher, T>{std::move(impl_).consume()}; }
};

template <Stdio Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr StdioPinned<Tier, T> mint_stdio(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return StdioPinned<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace stdio_pin {
    template <typename T> using NoStdio         = StdioPinned<Stdio::NoStdio,         T>;
    template <typename T> using BufferedWrite   = StdioPinned<Stdio::BufferedWrite,   T>;
    template <typename T> using UnbufferedWrite = StdioPinned<Stdio::UnbufferedWrite, T>;
    template <typename T> using InteractiveRead = StdioPinned<Stdio::InteractiveRead, T>;
}  // namespace stdio_pin

static_assert(sizeof(StdioPinned<Stdio::NoStdio,         int>)    == sizeof(int));
static_assert(sizeof(StdioPinned<Stdio::InteractiveRead, int>)    == sizeof(int));
static_assert(sizeof(StdioPinned<Stdio::UnbufferedWrite, double>) == sizeof(double));
static_assert(sizeof(StdioPinned<Stdio::NoStdio,         char>)   == sizeof(char));

namespace detail::stdio_pinned_self_test {

using NoStdioInt      = StdioPinned<Stdio::NoStdio,         int>;
using InteractiveInt  = StdioPinned<Stdio::InteractiveRead, int>;

inline constexpr NoStdioInt sd_default{};
static_assert(sd_default.peek() == 0);
static_assert(NoStdioInt::tier == Stdio::NoStdio);
static_assert(InteractiveInt::tier == Stdio::InteractiveRead);
static_assert(NoStdioInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(NoStdioInt::satisfies<Stdio::NoStdio>);
static_assert(NoStdioInt::satisfies<Stdio::InteractiveRead>);
static_assert( InteractiveInt::satisfies<Stdio::InteractiveRead>);
static_assert(!InteractiveInt::satisfies<Stdio::NoStdio>);
static_assert(!InteractiveInt::satisfies<Stdio::BufferedWrite>);

inline constexpr auto widened = NoStdioInt{42}.widen<Stdio::UnbufferedWrite>();
static_assert(widened.peek() == 42 && widened.tier == Stdio::UnbufferedWrite);

inline constexpr auto minted = mint_stdio<Stdio::BufferedWrite, int>(99);
static_assert(minted.peek() == 99 && minted.tier == Stdio::BufferedWrite);
static_assert(std::is_same_v<stdio_pin::NoStdio<int>, NoStdioInt>);
static_assert(std::is_same_v<stdio_pin::InteractiveRead<int>, InteractiveInt>);
static_assert(!std::is_same_v<NoStdioInt, InteractiveInt>);
static_assert(std::is_copy_constructible_v<NoStdioInt>);

inline void runtime_smoke_test() {
    int seed = 21;
    NoStdioInt n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();
    auto w = NoStdioInt{seed}.widen<Stdio::BufferedWrite>();
    if (w.peek() != 21 || w.tier != Stdio::BufferedWrite) std::abort();
    auto m = mint_stdio<Stdio::InteractiveRead, int>(seed);
    if (std::move(m).consume() != 21) std::abort();
    NoStdioInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::stdio_pinned_self_test

}  // namespace crucible::safety
