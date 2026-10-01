#pragma once

// Tropical min-plus semiring over the natural numbers with infinity,
// carrying a chain-lattice reading on the same carrier.  Staleness
// counts how many steps behind the current one a value is.  Zero is
// fresh and infinity means never observed.
//
// The two readings answer different questions and both are needed.
// Tropical addition is min and picks the freshest of competing
// estimates for one value.  Tropical multiplication is ordinary
// addition and accumulates a worst-case bound along a chain of
// operations.  The lattice join is max, so composing two graded values
// keeps the more pessimistic bound.
//
// Infinity is encoded as the largest representable value.  That keeps
// the element one word wide and makes the default comparison order the
// intended one.  The cost is that a finite staleness cannot use that
// value.  Saturating addition folds any overflow onto it, which is the
// right answer: a staleness past the representable range is unbounded
// for any practical purpose.

#include <foundation/Saturate.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/contracts/Pre.h>

#include <compare>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

struct StalenessSemiring {
    struct element_type {
        std::uint64_t value{0};

        [[nodiscard]] static constexpr element_type infinity() noexcept {
            return element_type{std::numeric_limits<std::uint64_t>::max()};
        }

        [[nodiscard]] constexpr bool is_infinite() const noexcept {
            return value == std::numeric_limits<std::uint64_t>::max();
        }
        [[nodiscard]] constexpr bool is_finite() const noexcept {
            return value != std::numeric_limits<std::uint64_t>::max();
        }

        [[nodiscard]] friend constexpr auto operator<=>(element_type, element_type) noexcept = default;
        [[nodiscard]] friend constexpr bool operator==(element_type, element_type) noexcept = default;
    };

    // A staler bound promises less about the value, and it is the weaker
    // claim.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{0}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return element_type::infinity(); }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a.value <= b.value; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.value < b.value ? b.value : a.value};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{b.value < a.value ? b.value : a.value};
    }

    [[nodiscard]] static constexpr element_type zero() noexcept { return top(); }
    [[nodiscard]] static constexpr element_type one() noexcept { return bottom(); }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept { return meet(a, b); }
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        if (a.is_infinite() || b.is_infinite()) {
            return top();
        }
        return element_type{::foundation::sat::add_sat(a.value, b.value)};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "StalenessSemiring"; }
};

namespace staleness {

inline constexpr StalenessSemiring::element_type fresh = StalenessSemiring::bottom();
inline constexpr StalenessSemiring::element_type infinite = StalenessSemiring::top();

[[nodiscard]] constexpr StalenessSemiring::element_type at(std::uint64_t n) noexcept {
    CRUCIBLE_PRE(n < std::numeric_limits<std::uint64_t>::max());
    return StalenessSemiring::element_type{n};
}

}  // namespace staleness

namespace detail {

// The carrier of a stale value.  The check file of this header and
// test/foundation/test_lattices_core.cpp name it.
template <typename T>
using StaleGraded = Graded<ModalityKind::Absolute, StalenessSemiring, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
