#pragma once

// Chain-order lattice ops over a scoped enum, and the base that states a
// whole chain lattice from its enum.
//
// ChainLatticeOps holds only leq, join and meet.  EnumChainLattice adds
// bottom(), top(), name() and the claim orientation.  It reads the first
// three by reflection and takes the orientation as an argument, and a
// chain lattice states only its enum, its orientation and its pinned
// grade.  Each lattice stays its own type: EnumChainLattice takes the
// derived lattice as an argument, and one
// `template <typename EnumT> ChainLattice` would give every lattice over
// the same enum a single type identity.
//
// The ops are constexpr and not consteval, and a runtime precondition of
// a consumer can call them under the enforce contract semantic.
//
// The pinned grade is shared as well.  Every lattice over a scoped enum
// publishes At<v>, the one-element lattice that fixes v in the type.
// PinnedAt is that lattice, written once.  A lattice derives its At from
// it and adds only the member that spells the pinned value in the
// lattice's own vocabulary.  The name of At<v> is built by reflection
// from the outer lattice's name and the enumerator identifier.  The
// self-tests at the end walk the enumerators by reflection, and a new
// enumerator reaches the name and the checks the moment it is declared.
//
// Each pinned grade has the element PinnedElement<v>, and the walk
// verify_pinned_at checks that.  A carrier graded on such an element has
// the layout of its payload, and the self-test at the end checks that
// layout one time for all of them.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/Modality.h>
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

// The one-element lattice that pins Value inside Outer.
//
// name() reads Outer::name() only when it is called.  Outer is still
// incomplete when its At is declared, and it is complete by the time a
// name is asked for, so the read must not move into the class body.
template <typename Outer, auto Value>
    requires std::is_scoped_enum_v<decltype(Value)>
struct PinnedAt {
    using element_type = PinnedElement<Value>;
    using enum_type = decltype(Value);
    using outer_lattice = Outer;

    static constexpr enum_type pinned = Value;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return pinned_at_name_v<Outer, Value>; }
};

// A chain lattice over the enumerators of EnumT, in declaration order.
// bottom() is the first enumerator and top() is the last, and name() is
// the identifier of Derived, each read by reflection.  The orientation is
// an argument, and a chain lattice cannot leave its orientation unstated.
// Derived declares its own At<v> over PinnedAt, because the name of At is
// the identity that a row hash folds.
//
// Derived is incomplete while this base is instantiated, and name() reads
// it only when it is called.  The underlying values must rise in
// declaration order, because leq compares them.  verify_chain_lattice
// checks that.
template <typename Derived, typename EnumT, ClaimOrientation Orientation>
    requires std::is_scoped_enum_v<EnumT>
          && (Orientation == ClaimOrientation::weaker_is_higher || Orientation == ClaimOrientation::stronger_is_higher)
struct EnumChainLattice : ChainLatticeOps<EnumT> {
    static constexpr ClaimOrientation claim_orientation = Orientation;

    [[nodiscard]] static constexpr EnumT bottom() noexcept { return [:enumerators_.front():]; }
    [[nodiscard]] static constexpr EnumT top() noexcept { return [:enumerators_.back():]; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return std::meta::identifier_of(^^Derived); }

private:
    static constexpr auto enumerators_ = std::define_static_array(std::meta::enumerators_of(^^EnumT));
    static_assert(!enumerators_.empty(), "EnumChainLattice: a chain needs at least one enumerator.");
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
// bounded lattice, its element is PinnedElement<e>, which is empty and
// converts back to e, its pinned member is e, and its name is neither
// empty nor the sentinel.  Two different enumerators give two different
// At types with two different names.  E is a parameter because a product
// lattice pins an enum that is not its element type.
template <typename L, typename E = typename L::element_type>
    requires std::is_scoped_enum_v<E>
[[nodiscard]] consteval bool verify_pinned_at() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        using AtA = typename L::template At<([:ea:])>;
        if (!BoundedLattice<AtA>) return false;
        if (!std::is_same_v<typename AtA::element_type, PinnedElement<([:ea:])>>) return false;
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

struct SmokeChainLattice : EnumChainLattice<SmokeChainLattice, SmokeTier, ClaimOrientation::stronger_is_higher> {
    template <SmokeTier T>
    struct At : PinnedAt<SmokeChainLattice, T> {
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

// The base reads the bounds and the name by reflection, and it takes the
// orientation as the argument states it.
static_assert(SmokeChainLattice::bottom() == SmokeTier::Lo);
static_assert(SmokeChainLattice::top() == SmokeTier::Hi);
static_assert(SmokeChainLattice::name() == "SmokeChainLattice");
static_assert(claim_orientation_v<SmokeChainLattice> == ClaimOrientation::stronger_is_higher);
static_assert(!GradableLattice<SmokeChainLattice>);
static_assert(claim_orientation_v<SmokeChainLattice::At<SmokeTier::Mid>> == ClaimOrientation::one_claim);

// A chain base with an unstated orientation, or with the one-claim
// orientation of a single element, does not compile.
template <ClaimOrientation Orientation>
concept chain_base_accepts = requires { typename EnumChainLattice<SmokeChainLattice, SmokeTier, Orientation>; };
static_assert(chain_base_accepts<ClaimOrientation::weaker_is_higher>);
static_assert(!chain_base_accepts<ClaimOrientation::unstated> && !chain_base_accepts<ClaimOrientation::one_claim>);

// The generic walk pins the shape; these cells pin the exact spellings
// that the reflection builds.
static_assert(SmokeChainLattice::At<SmokeTier::Mid>::name() == "SmokeChainLattice::At<Mid>");
static_assert(SmokeChainLattice::At<static_cast<SmokeTier>(9)>::name() == "SmokeChainLattice::At<?>");
static_assert(pinned_at_sentinel_v<SmokeChainLattice> == "SmokeChainLattice::At<?>");
static_assert(SmokeChainLattice::At<SmokeTier::Hi>::tier == SmokeTier::Hi);
static_assert(SmokeChainLattice::At<SmokeTier::Hi>::pinned == SmokeTier::Hi);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::enum_type, SmokeTier>);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::outer_lattice, SmokeChainLattice>);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::element_type::pinned_value_type, SmokeTier>);
static_assert(std::is_empty_v<SmokeChainLattice::At<SmokeTier::Lo>::element_type>);
static_assert(BoundedLattice<SmokeChainLattice::At<SmokeTier::Lo>>);

// Each pinned grade of each lattice has the element PinnedElement<v>, and
// verify_pinned_at checks that.  These cells check the layout of a
// carrier over that element: a class value and an arithmetic value each
// keep their size, alignment and trivial properties, under the modality
// that fixes a grade and under the one that takes the value back out.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using PinnedAbsolute = Graded<ModalityKind::Absolute, SmokeChainLattice::At<SmokeTier::Mid>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, double);

template <typename T_>
using PinnedComonad = Graded<ModalityKind::Comonad, SmokeChainLattice::At<SmokeTier::Hi>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, double);

// A chain declared out of order fails the declaration-order walk, and a
// chain whose bottom is not its first enumerator fails the bounds check.
// The negative direction of the generic self-test is witnessed here, and
// a refactor cannot turn it into a tautology.
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

// EnumChainLattice over the reversed enum reads the first enumerator as
// bottom.  That bottom sits above its top, and the result is not a
// lattice.
struct ReversedEnumChain : EnumChainLattice<ReversedEnumChain, ReversedTier, ClaimOrientation::stronger_is_higher> {};
static_assert(!Lattice<ReversedEnumChain>,
              "A chain base over an enum whose values fall in declaration order must not be a lattice.");

}  // namespace detail::chain_lattice_self_test

}  // namespace foundation::algebra::lattices
