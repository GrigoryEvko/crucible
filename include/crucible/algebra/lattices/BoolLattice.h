#pragma once

// One single-element lattice per predicate.
//
// A refined value exists only because the predicate held when it was
// built, and it cannot be mutated afterwards, so every such value sits
// at the same position: the predicate holds.  There is no position for
// "unknown" or "fails", because a value in either state was never
// constructed.  The element type is therefore empty and every operation
// is identity, and the emptiness is what lets the grade collapse to
// nothing in the wrapped value.
//
// Two different predicates give two different lattices, so nothing here
// relates them.  That one predicate implies another is a separate
// judgement, decided outside this type.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

template <typename Pred>
struct BoolLattice {
    struct element_type {
        using predicate_type = Pred;
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
    };

    using predicate_type = Pred;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return std::meta::display_string_of(^^Pred); }
};

namespace detail::bool_lattice_self_test {

// Only the predicate's identity matters to the lattice.  The check
// bodies are here because a predicate is expected to carry one, and they
// are exercised at construction by whatever wraps a value, never here.
struct positive {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept {
        return v > T{0};
    }
};
struct non_negative {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept {
        return v >= T{0};
    }
};
struct non_zero {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept {
        return v != T{0};
    }
};

static_assert(Lattice<BoolLattice<positive>>);
static_assert(BoundedLattice<BoolLattice<positive>>);
static_assert(Lattice<BoolLattice<non_negative>>);
static_assert(Lattice<BoolLattice<non_zero>>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<BoolLattice<positive>::element_type>);
static_assert(std::is_empty_v<BoolLattice<non_negative>::element_type>);
static_assert(std::is_empty_v<BoolLattice<non_zero>::element_type>);

static_assert(verify_bounded_lattice_axioms_at<BoolLattice<positive>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<BoolLattice<non_negative>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<BoolLattice<non_zero>>({}, {}, {}));

// display_string_of returns the simple name or the fully qualified one
// depending on the scope chain of the translation unit doing the
// including.  Match with ends_with, never with ==, or these assertions
// hold in one translation unit and fail in the next.
static_assert(BoolLattice<positive>::name().ends_with("positive"));
static_assert(BoolLattice<non_negative>::name().ends_with("non_negative"));
static_assert(BoolLattice<non_zero>::name().ends_with("non_zero"));

static_assert(std::is_same_v<BoolLattice<positive>::predicate_type, positive>);
static_assert(std::is_same_v<BoolLattice<positive>::element_type::predicate_type, positive>);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using RefinedPositive = Graded<ModalityKind::Absolute, BoolLattice<positive>, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, EightByteValue);
// The arithmetic witnesses pin the collapse across the
// trivially-default-constructible split as well as the class one.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, double);

inline void runtime_smoke_test() {
    using L = BoolLattice<positive>;
    L::element_type a{};
    L::element_type b{};
    [[maybe_unused]] bool l = L::leq(a, b);
    [[maybe_unused]] L::element_type j = L::join(a, b);
    [[maybe_unused]] L::element_type m = L::meet(a, b);

    OneByteValue v{42};
    RefinedPositive<OneByteValue> initial{v, L::bottom()};
    auto widened = initial.weaken(L::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(L::top());
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
}

}  // namespace detail::bool_lattice_self_test

}  // namespace crucible::algebra::lattices
