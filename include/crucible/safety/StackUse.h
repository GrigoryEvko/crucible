#pragma once

// The tier records how tightly a producer's stack footprint is bounded: a
// ConstantFrame producer cannot overflow, a BoundedByParam one is bounded by an
// argument, a BoundedDynamic one has a bound that is only known at run time, and
// an Unbounded one has none.  It is a ceiling on footprint, not a floor on
// proof, so the bottom tier is the safe one and a consumer admits a value whose
// tier is at or below the ceiling it imposes.  Widening up the chain is sound
// because over-stating the footprint is safe.
//
// The modality is Absolute because the tier describes the producer, not the
// content.  Mutating the wrapped value cannot change the frame that produced
// it, so mutable access needs no re-check.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/StackUseLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::StackUse;
using ::crucible::algebra::lattices::StackUseLattice;

template <StackUse Tier, typename T>
class [[nodiscard]] StackUsePinned {
public:
    using value_type = T;
    using lattice_type = StackUseLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    static constexpr StackUse tier = Tier;

private:
    graded_type impl_;

public:
    constexpr StackUsePinned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit StackUsePinned(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit StackUsePinned(std::in_place_t,
                                      Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                               && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr StackUsePinned(const StackUsePinned&) = default;
    constexpr StackUsePinned(StackUsePinned&&) = default;
    constexpr StackUsePinned& operator=(const StackUsePinned&) = default;
    constexpr StackUsePinned& operator=(StackUsePinned&&) = default;
    ~StackUsePinned() = default;

    [[nodiscard]] friend constexpr bool operator==(StackUsePinned const& a,
                                                   StackUsePinned const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(StackUsePinned& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(StackUsePinned& a, StackUsePinned& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <StackUse Ceiling>
    static constexpr bool satisfies = StackUseLattice::leq(Tier, Ceiling);

    template <StackUse Higher>
        requires(StackUseLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr StackUsePinned<Higher, T> widen() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return StackUsePinned<Higher, T>{this->peek()};
    }

    template <StackUse Higher>
        requires(StackUseLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr StackUsePinned<Higher, T> widen() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return StackUsePinned<Higher, T>{std::move(impl_).consume()};
    }
};

template <StackUse Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr StackUsePinned<Tier, T>
mint_stack_use(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return StackUsePinned<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace stack_use_pin {
template <typename T>
using ConstantFrame = StackUsePinned<StackUse::ConstantFrame, T>;
template <typename T>
using BoundedByParam = StackUsePinned<StackUse::BoundedByParam, T>;
template <typename T>
using BoundedDynamic = StackUsePinned<StackUse::BoundedDynamic, T>;
template <typename T>
using Unbounded = StackUsePinned<StackUse::Unbounded, T>;
}  // namespace stack_use_pin

static_assert(sizeof(StackUsePinned<StackUse::ConstantFrame, int>) == sizeof(int));
static_assert(sizeof(StackUsePinned<StackUse::Unbounded, int>) == sizeof(int));
static_assert(sizeof(StackUsePinned<StackUse::BoundedByParam, double>) == sizeof(double));
static_assert(sizeof(StackUsePinned<StackUse::ConstantFrame, char>) == sizeof(char));

namespace detail::stack_use_pinned_self_test {

using ConstInt = StackUsePinned<StackUse::ConstantFrame, int>;
using UnboundedInt = StackUsePinned<StackUse::Unbounded, int>;

inline constexpr ConstInt su_default{};
static_assert(su_default.peek() == 0);
static_assert(ConstInt::tier == StackUse::ConstantFrame);
static_assert(UnboundedInt::tier == StackUse::Unbounded);
static_assert(ConstInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(ConstInt::satisfies<StackUse::ConstantFrame>);
static_assert(ConstInt::satisfies<StackUse::Unbounded>);
static_assert(UnboundedInt::satisfies<StackUse::Unbounded>);
static_assert(!UnboundedInt::satisfies<StackUse::ConstantFrame>);
static_assert(!UnboundedInt::satisfies<StackUse::BoundedByParam>);

inline constexpr auto widened = ConstInt{42}.widen<StackUse::BoundedDynamic>();
static_assert(widened.peek() == 42 && widened.tier == StackUse::BoundedDynamic);

inline constexpr auto minted = mint_stack_use<StackUse::BoundedByParam, int>(99);
static_assert(minted.peek() == 99 && minted.tier == StackUse::BoundedByParam);
static_assert(std::is_same_v<stack_use_pin::ConstantFrame<int>, ConstInt>);
static_assert(std::is_same_v<stack_use_pin::Unbounded<int>, UnboundedInt>);
static_assert(!std::is_same_v<ConstInt, UnboundedInt>);
static_assert(std::is_copy_constructible_v<ConstInt>);

inline void runtime_smoke_test() {
    int seed = 15;
    ConstInt c{seed * 2};
    if (c.peek() != 30) std::abort();
    c.peek_mut() = 6;
    if (c.peek() != 6) std::abort();
    auto w = ConstInt{seed}.widen<StackUse::BoundedByParam>();
    if (w.peek() != 15 || w.tier != StackUse::BoundedByParam) std::abort();
    auto m = mint_stack_use<StackUse::Unbounded, int>(seed);
    if (std::move(m).consume() != 15) std::abort();
    ConstInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::stack_use_pinned_self_test

}  // namespace crucible::safety
