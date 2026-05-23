#pragma once

// ── crucible::safety::CallShapePinned<CallShape Tier, typename T> ────
//
// FIXY-V-242 (2/5): value-level Graded carrier for the V-240 CallShape
// axis (Direct ⊏ BoundedRecurses ⊏ Indirect ⊏ Virtual ⊏ Unbounded).
// Pins a function-result's dispatch / call-shape into the type.
//
//   Substrate: Graded<ModalityKind::Absolute, CallShapeLattice::At<Tier>, T>
//   Regime:    1 (zero-cost EBO collapse — sizeof == sizeof(T)).
//
// Absolute modality (the dispatch shape is a static property of how the
// value was produced; mutating T cannot change it).  CAPABILITY-CEILING
// semantics, mirror image of Witness: bottom (Direct) is the safest /
// most-analyzable; top (Unbounded) the least.
//
//   satisfies<C> := CallShapeLattice::leq(Tier, C)   — within ceiling C
//   widen<Higher>()                                   — UP the chain only
//
// Forge hot-path admission imposes ceiling Direct (fully static,
// inlinable); only a CallShapePinned<Direct, T> satisfies it.  The
// BoundedRecurses recursion bound N (see V-240) is carried separately
// by the V-245 recurses<N> grant — it is NOT a chain tier here.
//
// §XXI: `mint_call_shape<Tier, T>(args...)` (token mint).  HS14 neg
// fixtures: neg_call_shape_widen_to_lower.cpp + neg_call_shape_mint_wrong_arg.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
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
    using value_type   = T;
    using lattice_type = CallShapeLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;
    static constexpr CallShape tier = Tier;

private:
    graded_type impl_;

public:
    constexpr CallShapePinned() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit CallShapePinned(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit CallShapePinned(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr CallShapePinned(const CallShapePinned&)            = default;
    constexpr CallShapePinned(CallShapePinned&&)                 = default;
    constexpr CallShapePinned& operator=(const CallShapePinned&) = default;
    constexpr CallShapePinned& operator=(CallShapePinned&&)      = default;
    ~CallShapePinned()                                           = default;

    [[nodiscard]] friend constexpr bool operator==(
        CallShapePinned const& a, CallShapePinned const& b) noexcept(
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

    constexpr void swap(CallShapePinned& other)
        noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(CallShapePinned& a, CallShapePinned& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <CallShape Ceiling>
    static constexpr bool satisfies = CallShapeLattice::leq(Tier, Ceiling);

    template <CallShape Higher>
        requires (CallShapeLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr CallShapePinned<Higher, T> widen() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return CallShapePinned<Higher, T>{this->peek()}; }

    template <CallShape Higher>
        requires (CallShapeLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr CallShapePinned<Higher, T> widen() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return CallShapePinned<Higher, T>{std::move(impl_).consume()}; }
};

template <CallShape Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr CallShapePinned<Tier, T> mint_call_shape(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return CallShapePinned<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace call_shape_pin {
    template <typename T> using Direct          = CallShapePinned<CallShape::Direct,          T>;
    template <typename T> using BoundedRecurses = CallShapePinned<CallShape::BoundedRecurses, T>;
    template <typename T> using Indirect        = CallShapePinned<CallShape::Indirect,        T>;
    template <typename T> using Virtual         = CallShapePinned<CallShape::Virtual,         T>;
    template <typename T> using Unbounded       = CallShapePinned<CallShape::Unbounded,       T>;
}  // namespace call_shape_pin

static_assert(sizeof(CallShapePinned<CallShape::Direct,    int>)    == sizeof(int));
static_assert(sizeof(CallShapePinned<CallShape::Unbounded, int>)    == sizeof(int));
static_assert(sizeof(CallShapePinned<CallShape::Virtual,   double>) == sizeof(double));
static_assert(sizeof(CallShapePinned<CallShape::Direct,    char>)   == sizeof(char));

namespace detail::call_shape_pinned_self_test {

using DirectInt    = CallShapePinned<CallShape::Direct,    int>;
using UnboundedInt = CallShapePinned<CallShape::Unbounded, int>;

inline constexpr DirectInt cs_default{};
static_assert(cs_default.peek() == 0);
static_assert(DirectInt::tier == CallShape::Direct);
static_assert(UnboundedInt::tier == CallShape::Unbounded);
static_assert(DirectInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(DirectInt::satisfies<CallShape::Direct>);
static_assert(DirectInt::satisfies<CallShape::Unbounded>);
static_assert( UnboundedInt::satisfies<CallShape::Unbounded>);
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
