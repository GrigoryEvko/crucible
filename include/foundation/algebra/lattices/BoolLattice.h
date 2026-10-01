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

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>

#include <meta>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

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

namespace detail {

// The predicates and the carrier of the BoolLattice checks.  The check
// file of this header and test/foundation/test_lattices_core.cpp name
// them, so they live here and not in the check file.

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

template <typename T>
using RefinedPositive = Graded<ModalityKind::Absolute, BoolLattice<positive>, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
