#pragma once

// The tier records the dispatch a producer needs, from a Direct call that is
// fully static and inlinable, through bounded recursion, an indirect call and a
// virtual one, up to an Unbounded shape that resists analysis entirely.  It is a
// ceiling on dispatch freedom, not a floor on proof, so the bottom tier is the
// analyzable one and a consumer admits a value whose tier is at or below the
// ceiling it imposes.  Widening up the chain is sound because over-stating what
// dispatch a producer needs is safe.  The bound on a bounded-recursion producer
// is not a tier here and is carried separately.
//
// The modality is Absolute because the tier describes the producer, not the
// content.  Mutating the wrapped value cannot change how it was reached, so
// mutable access needs no re-check.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/CallShapeLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::CallShape;
using ::crucible::algebra::lattices::CallShapeLattice;

template <CallShape Tier, typename T>
class [[nodiscard]] CallShapePinned {
public:
    using value_type = T;
    using lattice_type = CallShapeLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    static constexpr CallShape tier = Tier;

private:
    graded_type impl_;

public:
    constexpr CallShapePinned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit CallShapePinned(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit CallShapePinned(std::in_place_t,
                                       Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr CallShapePinned(const CallShapePinned&) = default;
    constexpr CallShapePinned(CallShapePinned&&) = default;
    constexpr CallShapePinned& operator=(const CallShapePinned&) = default;
    constexpr CallShapePinned& operator=(CallShapePinned&&) = default;
    ~CallShapePinned() = default;

    [[nodiscard]] friend constexpr bool operator==(CallShapePinned const& a,
                                                   CallShapePinned const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(CallShapePinned& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(CallShapePinned& a, CallShapePinned& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <CallShape Ceiling>
    static constexpr bool satisfies = CallShapeLattice::leq(Tier, Ceiling);

    template <CallShape Higher>
        requires(CallShapeLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr CallShapePinned<Higher, T> widen() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return CallShapePinned<Higher, T>{this->peek()};
    }

    template <CallShape Higher>
        requires(CallShapeLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr CallShapePinned<Higher, T> widen() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return CallShapePinned<Higher, T>{std::move(impl_).consume()};
    }
};

template <CallShape Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr CallShapePinned<Tier, T>
mint_call_shape(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return CallShapePinned<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace call_shape_pin {
template <typename T>
using Direct = CallShapePinned<CallShape::Direct, T>;
template <typename T>
using BoundedRecurses = CallShapePinned<CallShape::BoundedRecurses, T>;
template <typename T>
using Indirect = CallShapePinned<CallShape::Indirect, T>;
template <typename T>
using Virtual = CallShapePinned<CallShape::Virtual, T>;
template <typename T>
using Unbounded = CallShapePinned<CallShape::Unbounded, T>;
}  // namespace call_shape_pin

static_assert(sizeof(CallShapePinned<CallShape::Direct, int>) == sizeof(int));
static_assert(sizeof(CallShapePinned<CallShape::Unbounded, int>) == sizeof(int));
static_assert(sizeof(CallShapePinned<CallShape::Virtual, double>) == sizeof(double));
static_assert(sizeof(CallShapePinned<CallShape::Direct, char>) == sizeof(char));

namespace detail::call_shape_pinned_self_test {

using DirectInt = CallShapePinned<CallShape::Direct, int>;
using UnboundedInt = CallShapePinned<CallShape::Unbounded, int>;

inline constexpr DirectInt cs_default{};
static_assert(cs_default.peek() == 0);
static_assert(DirectInt::tier == CallShape::Direct);
static_assert(UnboundedInt::tier == CallShape::Unbounded);
static_assert(DirectInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(DirectInt::satisfies<CallShape::Direct>);
static_assert(DirectInt::satisfies<CallShape::Unbounded>);
static_assert(UnboundedInt::satisfies<CallShape::Unbounded>);
static_assert(!UnboundedInt::satisfies<CallShape::Direct>);
static_assert(!UnboundedInt::satisfies<CallShape::Indirect>);

inline constexpr auto widened = DirectInt{42}.widen<CallShape::Virtual>();
static_assert(widened.peek() == 42 && widened.tier == CallShape::Virtual);

inline constexpr auto minted = mint_call_shape<CallShape::Indirect, int>(99);
static_assert(minted.peek() == 99 && minted.tier == CallShape::Indirect);
static_assert(std::is_same_v<call_shape_pin::Direct<int>, DirectInt>);
static_assert(std::is_same_v<call_shape_pin::Unbounded<int>, UnboundedInt>);
static_assert(!std::is_same_v<DirectInt, UnboundedInt>);
static_assert(std::is_copy_constructible_v<DirectInt>);

inline void runtime_smoke_test() {
    int seed = 13;
    DirectInt d{seed * 2};
    if (d.peek() != 26) std::abort();
    d.peek_mut() = 4;
    if (d.peek() != 4) std::abort();
    auto w = DirectInt{seed}.widen<CallShape::Indirect>();
    if (w.peek() != 13 || w.tier != CallShape::Indirect) std::abort();
    auto m = mint_call_shape<CallShape::Virtual, int>(seed);
    if (std::move(m).consume() != 13) std::abort();
    DirectInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::call_shape_pinned_self_test

}  // namespace crucible::safety
