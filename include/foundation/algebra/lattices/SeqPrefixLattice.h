#pragma once

// Prefix order over sequences of Element, encoded as a prefix length.
//
// On arbitrary sequences the prefix order is only a semi-lattice: two
// sequences with no common extension have no join.  Every prefix graded
// here belongs to one append-only stream, so any two of them are
// prefixes of the same canonical sequence.  On that domain the order
// collapses to a total order on length, the join is always the longer
// one, and the structure is a genuine lattice.
//
// The grade is therefore a length, not the sequence itself.  Storing the
// prefix would cost bytes proportional to the stream and make each
// lattice operation a prefix comparison, where a length comparison is
// one integer compare on a fixed-size grade.
//
// Element never appears in the grade.  It is a tag that keeps the
// lattices, and their Length grades, distinct per stream.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

template <typename Element>
struct Length {
    std::size_t length{0};

    [[nodiscard]] static constexpr Length at(std::size_t n) noexcept { return Length{n}; }

    [[nodiscard]] friend constexpr auto operator<=>(Length, Length) noexcept = default;
    [[nodiscard]] friend constexpr bool operator==(Length, Length) noexcept = default;
};

template <typename Element>
struct SeqPrefixLattice {
    using element_type = Length<Element>;
    using sequence_element_type = Element;

    // A longer prefix claims more of the stream, and it is the stronger
    // claim.  An append-only container derives its prefix through
    // grade_of below, and Graded needs no orientation for a derived grade.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{0}; }
    [[nodiscard]] static constexpr element_type top() noexcept {
        // A synthesized ceiling.  The order has no greatest element, but
        // the bounded-lattice concept needs a representable top(), and
        // SIZE_MAX is unreachable: a stream that long would exhaust the
        // address space first.
        return element_type{std::numeric_limits<std::size_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a.length <= b.length; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.length < b.length ? b.length : a.length};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{b.length < a.length ? b.length : a.length};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "SeqPrefixLattice"; }

    // A graded container already stores its own length.  Supplying
    // grade_of opts into the substrate's derived-grade form, which reads
    // the grade from the value instead of storing a second copy of it.
    template <typename Container>
        requires requires(Container const& c) {
            { c.size() } -> std::convertible_to<std::size_t>;
        }
    [[nodiscard]] static constexpr element_type grade_of(Container const& c) noexcept {
        return element_type{c.size()};
    }
};

namespace detail {

// The event, the order, the log and the carrier of the SeqPrefixLattice
// checks.  The check file of this header and
// test/foundation/test_lattices_core.cpp name them, so they live here and
// not in the check file.
struct EventA {};

using LatA = SeqPrefixLattice<EventA>;

// An append-only container holds its own length.
struct MiniLog {
    std::size_t count{0};
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
};

template <typename T>
using AppendOnlyGraded = Graded<ModalityKind::Absolute, SeqPrefixLattice<EventA>, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
