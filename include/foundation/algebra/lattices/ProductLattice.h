#pragma once

// element_type is an in-house aggregate rather than a std::tuple because the
// libstdc++ tuple does not aggressively apply the empty base optimization to
// its members.  A tuple of two empty element types costs two bytes, one per
// empty component, where the aggregate below costs the one-byte language
// minimum for the whole product.
//
// This file declares the variadic primary template, and then the
// specializations for two components, for none and for any other count.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

template <typename... Ls>
struct ProductLattice;

template <typename L1, typename L2>
struct ProductLattice<L1, L2> {
    static_assert(Lattice<L1>, "ProductLattice<L1, L2>: L1 must satisfy the Lattice concept.");
    static_assert(Lattice<L2>, "ProductLattice<L1, L2>: L2 must satisfy the Lattice concept.");

    // Defaulting operator== is sound because every lattice element type is
    // required to publish one of its own.
    struct element_type {
        [[no_unique_address]] typename L1::element_type first{};
        [[no_unique_address]] typename L2::element_type second{};

        [[nodiscard]] constexpr bool operator==(const element_type&) const noexcept = default;
    };

    using first_lattice = L1;
    using second_lattice = L2;

    static constexpr std::size_t arity = 2;

    // The orientation the two components share (ClaimOrientation.h).
    static constexpr ClaimOrientation claim_orientation = product_orientation<L1, L2>();

    template <std::size_t I>
        requires(I < 2)
    using nth_lattice = std::conditional_t<I == 0, L1, L2>;

    template <std::size_t I>
        requires(I < 2)
    [[nodiscard]] static constexpr auto& get(element_type& e) noexcept {
        if constexpr (I == 0)
            return e.first;
        else
            return e.second;
    }

    template <std::size_t I>
        requires(I < 2)
    [[nodiscard]] static constexpr auto const& get(element_type const& e) noexcept {
        if constexpr (I == 0)
            return e.first;
        else
            return e.second;
    }

    [[nodiscard]] static constexpr element_type bottom() noexcept
        requires BoundedBelowLattice<L1> && BoundedBelowLattice<L2>
    {
        return element_type{L1::bottom(), L2::bottom()};
    }

    [[nodiscard]] static constexpr element_type top() noexcept
        requires BoundedAboveLattice<L1> && BoundedAboveLattice<L2>
    {
        return element_type{L1::top(), L2::top()};
    }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return L1::leq(a.first, b.first) && L2::leq(a.second, b.second);
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{L1::join(a.first, b.first), L2::join(a.second, b.second)};
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{L1::meet(a.first, b.first), L2::meet(a.second, b.second)};
    }

    // The name is a fixed token rather than a composition of the component
    // names.  Building the composed string needs define_static_string glue and
    // pays compile time for a diagnostic the carrier's reflected display
    // already gives.  A formatter that wants the components reads the
    // first_lattice and second_lattice typedefs instead.
    [[nodiscard]] static consteval std::string_view name() noexcept { return "Product<L1xL2>"; }
};

template <>
struct ProductLattice<> {
    struct element_type {
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
    };

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "Product<>"; }
};

// The N-ary product stores one base class per component rather than a
// recursive head-and-tail pair.  A cons-list nests element types as deep as
// the arity, so its layout has to be resolved one level at a time, and its
// get<I> recurses through I template instantiations.  Flat bases let the empty
// base optimization run over every slot in a single layout pass, and get<I>
// becomes a single cast to the I-th base.
//
// The binary case keeps a specialization of its own because it exposes the
// first and second member names that callers already use.  The N-ary primary
// would handle two components correctly, but under get<I> spelling only.

namespace detail {

// The index parameter is what makes each slot a distinct type.  Two components
// with the same lattice would otherwise name the same base class twice, which
// is ill-formed.
template <std::size_t I, typename L>
struct ProductSlot {
    [[no_unique_address]] typename L::element_type value{};

    [[nodiscard]] constexpr bool operator==(const ProductSlot&) const noexcept = default;
};

template <typename Indices, typename... Ls>
struct ProductElementImpl;

template <std::size_t... Is, typename... Ls>
struct ProductElementImpl<std::index_sequence<Is...>, Ls...> : ProductSlot<Is, Ls>... {
    [[nodiscard]] constexpr bool operator==(const ProductElementImpl&) const noexcept = default;
};

}  // namespace detail

template <typename... Ls>
    requires(sizeof...(Ls) != 2)
struct ProductLattice<Ls...> {
    static_assert((Lattice<Ls> && ...), "ProductLattice<Ls...>: every L_i must satisfy the Lattice concept.");

    static constexpr std::size_t arity = sizeof...(Ls);

    // The orientation the components share (ClaimOrientation.h).
    static constexpr ClaimOrientation claim_orientation = product_orientation<Ls...>();

    using element_type = detail::ProductElementImpl<std::make_index_sequence<sizeof...(Ls)>, Ls...>;

    template <std::size_t I>
        requires(I < sizeof...(Ls))
    using nth_lattice = Ls...[I];

    template <std::size_t I>
        requires(I < sizeof...(Ls))
    [[nodiscard]] static constexpr auto& get(element_type& e) noexcept {
        return static_cast<detail::ProductSlot<I, Ls...[I]>&>(e).value;
    }

    template <std::size_t I>
        requires(I < sizeof...(Ls))
    [[nodiscard]] static constexpr auto const& get(element_type const& e) noexcept {
        return static_cast<detail::ProductSlot<I, Ls...[I]> const&>(e).value;
    }

    [[nodiscard]] static constexpr element_type bottom() noexcept
        requires(BoundedBelowLattice<Ls> && ...)
    {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value = Ls...[Is] ::bottom()), ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static constexpr element_type top() noexcept
        requires(BoundedAboveLattice<Ls> && ...)
    {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value = Ls...[Is] ::top()), ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static constexpr bool leq(element_type const& a, element_type const& b) noexcept {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (Ls...[Is] ::leq(get<Is>(a), get<Is>(b)) && ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
    }

    [[nodiscard]] static constexpr element_type join(element_type const& a, element_type const& b) noexcept {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value =
                  Ls...[Is] ::join(get<Is>(a), get<Is>(b))),
             ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static constexpr element_type meet(element_type const& a, element_type const& b) noexcept {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value =
                  Ls...[Is] ::meet(get<Is>(a), get<Is>(b))),
             ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "Product<L1x...xLn>"; }
};

namespace detail {

// The component, the products and the carriers of the ProductLattice
// checks.  The check file of this header and
// test/foundation/test_lattices_bands.cpp name them, so they live here
// and not in the check file.

// This lattice is a use count, where the larger count is the weaker
// claim.  A product of two of them can then be stored beside a value.
struct U8MinMax {
    using element_type = std::uint8_t;
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 255; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a >= b ? a : b; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a <= b ? a : b; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "U8MinMax"; }
};

using P_u8u8 = ProductLattice<U8MinMax, U8MinMax>;
using P_u8u8u8 = ProductLattice<U8MinMax, U8MinMax, U8MinMax>;

template <typename T>
using BudgetU8U8 = Graded<ModalityKind::Absolute, P_u8u8, T>;
template <typename T>
using Budgeted3U8 = Graded<ModalityKind::Absolute, P_u8u8u8, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
