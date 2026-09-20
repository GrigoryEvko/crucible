#pragma once

// Chain-order lattice ops over a scoped enum.  Only the ops are shared.
// A derived lattice supplies its own bottom(), top(), name() and any
// per-tier nested templates.  Folding the whole lattice into one
// `template <typename EnumT> ChainLattice` would give every lattice over
// the same enum a single type identity, and each lattice must stay
// distinct.
//
// The ops are constexpr and not consteval, so a consumer's runtime
// precondition can call them under the enforce contract semantic.
//
// The pinned grade is shared as well.  Every lattice over a scoped enum
// publishes At<v>, the one-element lattice that fixes v in the type.
// PinnedAt is that lattice, written once.  A lattice derives its At from
// it and adds only the member that spells the pinned value in the
// lattice's own vocabulary.  The name of At<v> is built by reflection
// from the outer lattice's name and the enumerator identifier, and the
// self-tests at the end walk the enumerators by reflection, so a new
// enumerator reaches the name and the checks the moment it is declared.

#include <foundation/algebra/Lattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// A plain `enum E : int` satisfies std::is_enum_v but converts implicitly
// to its underlying integer, which would let arithmetic on a tier value
// compile.  Scoped enums only.
template <typename EnumT>
    requires std::is_scoped_enum_v<EnumT>
struct ChainLatticeOps {
    using element_type = EnumT;

    [[nodiscard]] static constexpr bool leq(EnumT a, EnumT b) noexcept {
        return std::to_underlying(a) <= std::to_underlying(b);
    }
    [[nodiscard]] static constexpr EnumT join(EnumT a, EnumT b) noexcept { return leq(a, b) ? b : a; }
    [[nodiscard]] static constexpr EnumT meet(EnumT a, EnumT b) noexcept { return leq(a, b) ? a : b; }
};

// The element of a pinned grade.  It is empty, so a carrier graded on it
// collapses to the size of its payload.  It converts to the one
// enumerator it pins, so a holder can read the value back.
template <auto Value>
    requires std::is_scoped_enum_v<decltype(Value)>
struct PinnedElement {
    using pinned_value_type = decltype(Value);

    [[nodiscard]] constexpr operator pinned_value_type() const noexcept { return Value; }
    [[nodiscard]] constexpr bool operator==(PinnedElement) const noexcept { return true; }
};

namespace detail {

// "Outer::At<Name>", or "Outer::At<?>" when Value names no enumerator.
// The text lives in static storage, so the view stays valid for the
// whole program.
template <typename Outer, auto Value>
[[nodiscard]] consteval std::string_view make_pinned_at_name() {
    using E = decltype(Value);
    std::string text{Outer::name()};
    text += "::At<";
    const std::string_view identifier = ::foundation::reflect::enum_name(Value);
    if (identifier == ::foundation::reflect::unknown_enum_sentinel<E>) {
        text += '?';
    } else {
        text += identifier;
    }
    text += '>';
    return std::define_static_string(text);
}

template <typename Outer>
[[nodiscard]] consteval std::string_view make_pinned_at_sentinel() {
    std::string text{Outer::name()};
    text += "::At<?>";
    return std::define_static_string(text);
}

}  // namespace detail

template <typename Outer, auto Value>
inline constexpr std::string_view pinned_at_name_v = detail::make_pinned_at_name<Outer, Value>();

// The name that Outer::At<v>::name() gives for a v outside the enum.
template <typename Outer>
inline constexpr std::string_view pinned_at_sentinel_v = detail::make_pinned_at_sentinel<Outer>();

// The one-element lattice that pins Value inside Outer.  Element is the
// grade type.  A lattice passes its own derivative of PinnedElement so
// that the element keeps the lattice's spelling of the value alias.
//
// name() reads Outer::name() only when it is called.  Outer is still
// incomplete when its At is declared, and it is complete by the time a
// name is asked for, so the read must not move into the class body.
template <typename Outer, auto Value, typename Element = PinnedElement<Value>>
    requires std::is_scoped_enum_v<decltype(Value)>
struct PinnedAt {
    using element_type = Element;
    using enum_type = decltype(Value);
    using outer_lattice = Outer;

    static_assert(std::is_empty_v<Element>, "PinnedAt: the element must stay empty so that a carrier graded on "
                                            "it collapses to the size of its payload.");

    static constexpr enum_type pinned = Value;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return pinned_at_name_v<Outer, Value>; }
};

template <typename ChainLattice>
[[nodiscard]] consteval bool verify_chain_lattice_exhaustive() noexcept {
    using EnumT = typename ChainLattice::element_type;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^EnumT));
    // `template for` unrolls into successive scopes that each declare the
    // induction variable, so -Wshadow fires on the body.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_bounded_lattice_axioms_at<ChainLattice>([:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename ChainLattice>
[[nodiscard]] consteval bool verify_chain_lattice_distributive_exhaustive() noexcept {
    using EnumT = typename ChainLattice::element_type;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^EnumT));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_distributive_lattice<ChainLattice>([:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// For a lattice over an enum whose order is not a chain.  The bounded
// axioms must hold at every triple, and leq, join and meet must agree at
// every pair: leq(a, b) holds exactly when meet(a, b) is a and join(a, b)
// is b.  Every element sits between bottom and top.
template <typename L>
    requires std::is_scoped_enum_v<typename L::element_type>
[[nodiscard]] consteval bool verify_enum_lattice_exhaustive() noexcept {
    using EnumT = typename L::element_type;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^EnumT));
    if (!verify_chain_lattice_exhaustive<L>()) {
        return false;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        if (!L::leq(L::bottom(), [:ea:]) || !L::leq([:ea:], L::top())) {
            return false;
        }
        template for (constexpr auto eb : enumerators) {
            const bool by_leq = L::leq([:ea:], [:eb:]);
            const bool by_meet = (L::meet([:ea:], [:eb:]) == [:ea:]);
            const bool by_join = (L::join([:ea:], [:eb:]) == [:eb:]);
            if (by_leq != by_meet || by_leq != by_join) {
                return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Walks every enumerator of E and checks the shape of L::At<e>: it is a
// bounded lattice, its element is empty and converts back to e, its
// pinned member is e, and its name is neither empty nor the sentinel.
// Two different enumerators give two different At types with two
// different names.  E is a parameter because a product lattice pins an
// enum that is not its element type.
template <typename L, typename E = typename L::element_type>
    requires std::is_scoped_enum_v<E>
[[nodiscard]] consteval bool verify_pinned_at() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        using AtA = typename L::template At<([:ea:])>;
        if (!BoundedLattice<AtA>) return false;
        if (!std::is_empty_v<typename AtA::element_type>) return false;
        if (static_cast<E>(typename AtA::element_type{}) != [:ea:]) return false;
        if (AtA::pinned != [:ea:]) return false;
        if (AtA::name().empty()) return false;
        if (AtA::name() == pinned_at_sentinel_v<L>) return false;
        template for (constexpr auto eb : enumerators) {
            using AtB = typename L::template At<([:eb:])>;
            if constexpr ([:ea:] != [:eb:]) {
                if (std::is_same_v<AtA, AtB>) return false;
                if (AtA::name() == AtB::name()) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// The whole self-test of a chain lattice over an enum.  The order must
// be the declaration order: bottom is the first enumerator, top is the
// last, leq(a, b) holds exactly when a is declared at or before b, join
// is the later of the two and meet the earlier.  Every enumerator has a
// reflected name that is not the sentinel.  Then the exhaustive axiom
// checks and the pinned-grade walk run.
//
// The name follows the verify_* family rather than the self-test
// namespaces, because every lattice header has a namespace
// detail::<x>_self_test beside a namespace detail::chain_lattice_self_test,
// and a function of that name would be hidden inside all of them.
template <typename L>
    requires std::is_scoped_enum_v<typename L::element_type>
[[nodiscard]] consteval bool verify_chain_lattice() noexcept {
    using EnumT = typename L::element_type;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^EnumT));
    if constexpr (enumerators.empty()) {
        return false;
    } else {
        if (!BoundedLattice<L>) return false;
        if (L::name().empty()) return false;
        if (L::bottom() != [:enumerators.front():]) return false;
        if (L::top() != [:enumerators.back():]) return false;
        if (!verify_chain_lattice_exhaustive<L>()) return false;
        if (!verify_chain_lattice_distributive_exhaustive<L>()) return false;
        std::size_t position_a = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
        template for (constexpr auto ea : enumerators) {
            const std::string_view identifier = ::foundation::reflect::enum_name([:ea:]);
            if (identifier.empty() || identifier == ::foundation::reflect::unknown_enum_sentinel<EnumT>) {
                return false;
            }
            std::size_t position_b = 0;
            template for (constexpr auto eb : enumerators) {
                const bool a_first = position_a <= position_b;
                if (L::leq([:ea:], [:eb:]) != a_first) return false;
                if (L::join([:ea:], [:eb:]) != (a_first ? [:eb:] : [:ea:])) return false;
                if (L::meet([:ea:], [:eb:]) != (a_first ? [:ea:] : [:eb:])) return false;
                ++position_b;
            }
            ++position_a;
        }
#pragma GCC diagnostic pop
        return verify_pinned_at<L, EnumT>();
    }
}

// The self-test enum is independent of every production caller, so the
// base is verified for an arbitrary scoped-enum ordinal layout.
namespace detail::chain_lattice_self_test {

enum class SmokeTier : std::uint8_t {
    Lo = 0,
    Mid = 1,
    Hi = 2
};

struct SmokeChainLattice : ChainLatticeOps<SmokeTier> {
    [[nodiscard]] static constexpr SmokeTier bottom() noexcept { return SmokeTier::Lo; }
    [[nodiscard]] static constexpr SmokeTier top() noexcept { return SmokeTier::Hi; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "SmokeChainLattice"; }

    template <SmokeTier T>
    struct AtElement : PinnedElement<T> {
        using smoke_tier_value_type = SmokeTier;
    };

    template <SmokeTier T>
    struct At : PinnedAt<SmokeChainLattice, T, AtElement<T>> {
        static constexpr SmokeTier tier = T;
    };
};

static_assert(verify_chain_lattice_exhaustive<SmokeChainLattice>(),
              "ChainLatticeOps must satisfy bounded-lattice axioms for an "
              "arbitrary scoped enum");
static_assert(verify_chain_lattice_distributive_exhaustive<SmokeChainLattice>(),
              "ChainLatticeOps must satisfy distributive-lattice axioms for "
              "an arbitrary scoped enum");
static_assert(verify_enum_lattice_exhaustive<SmokeChainLattice>());
static_assert(verify_chain_lattice<SmokeChainLattice>(),
              "The generic chain self-test must accept the smoke chain, which "
              "is declared in order with one At per tier.");

// The generic walk pins the shape; these cells pin the exact spellings
// that the reflection builds.
static_assert(SmokeChainLattice::At<SmokeTier::Mid>::name() == "SmokeChainLattice::At<Mid>");
static_assert(SmokeChainLattice::At<static_cast<SmokeTier>(9)>::name() == "SmokeChainLattice::At<?>");
static_assert(pinned_at_sentinel_v<SmokeChainLattice> == "SmokeChainLattice::At<?>");
static_assert(SmokeChainLattice::At<SmokeTier::Hi>::tier == SmokeTier::Hi);
static_assert(SmokeChainLattice::At<SmokeTier::Hi>::pinned == SmokeTier::Hi);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::enum_type, SmokeTier>);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::outer_lattice, SmokeChainLattice>);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::element_type::smoke_tier_value_type, SmokeTier>);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::element_type::pinned_value_type, SmokeTier>);
static_assert(std::is_empty_v<SmokeChainLattice::At<SmokeTier::Lo>::element_type>);
static_assert(BoundedLattice<SmokeChainLattice::At<SmokeTier::Lo>>);

// A chain declared out of order fails the declaration-order walk, and a
// chain whose bottom is not its first enumerator fails the bounds check.
// The negative direction of the generic self-test is witnessed here so
// that a refactor cannot turn it into a tautology.
enum class ReversedTier : std::uint8_t {
    Hi = 2,
    Mid = 1,
    Lo = 0
};

struct ReversedChainLattice : ChainLatticeOps<ReversedTier> {
    [[nodiscard]] static constexpr ReversedTier bottom() noexcept { return ReversedTier::Lo; }
    [[nodiscard]] static constexpr ReversedTier top() noexcept { return ReversedTier::Hi; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "ReversedChainLattice"; }

    template <ReversedTier T>
    struct At : PinnedAt<ReversedChainLattice, T> {
        static constexpr ReversedTier tier = T;
    };
};

static_assert(verify_chain_lattice_exhaustive<ReversedChainLattice>(),
              "The lattice laws do not care about declaration order.");
static_assert(!verify_chain_lattice<ReversedChainLattice>(),
              "The generic chain self-test must reject a chain whose declaration "
              "order is not its lattice order.");
static_assert(verify_pinned_at<ReversedChainLattice>(), "The pinned-grade walk does not depend on declaration order.");

}  // namespace detail::chain_lattice_self_test

}  // namespace foundation::algebra::lattices
