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

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

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
        return element_type{std::max(a.length, b.length)};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{std::min(a.length, b.length)};
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

namespace detail::seq_prefix_lattice_self_test {

struct EventA {};
struct EventB {};

using LatA = SeqPrefixLattice<EventA>;
using LatB = SeqPrefixLattice<EventB>;

static_assert(Lattice<LatA>);
static_assert(BoundedBelowLattice<LatA>);
static_assert(BoundedAboveLattice<LatA>);
static_assert(BoundedLattice<LatA>);
static_assert(Lattice<LatB>);
static_assert(BoundedLattice<LatB>);

static_assert(!std::is_same_v<LatA, LatB>);
static_assert(!std::is_same_v<LatA::element_type, LatB::element_type>);
static_assert(!std::is_convertible_v<LatA::element_type, LatB::element_type>);
static_assert(!std::is_convertible_v<LatB::element_type, LatA::element_type>);

static_assert(!std::is_empty_v<LatA::element_type>);
static_assert(sizeof(LatA::element_type) == sizeof(std::size_t));
static_assert(alignof(LatA::element_type) == alignof(std::size_t));

static_assert(LatA::element_type{} == LatA::bottom());
static_assert(LatA::bottom().length == 0);
static_assert(LatA::top().length == std::numeric_limits<std::size_t>::max());

static_assert(Length<EventA>{0} < Length<EventA>{1});
static_assert(Length<EventA>{100} == Length<EventA>{100});
static_assert(Length<EventA>{200} > Length<EventA>{199});

static_assert(Length<EventA>::at(0) == Length<EventA>{0});
static_assert(Length<EventA>::at(100) == Length<EventA>{100});
static_assert(Length<EventA>::at(0) == LatA::bottom());

constexpr Length<EventA> ε = LatA::bottom();
constexpr Length<EventA> short_ = Length<EventA>{1};
constexpr Length<EventA> mid = Length<EventA>{1024};
constexpr Length<EventA> longish = Length<EventA>{1000000};
constexpr Length<EventA> ω = LatA::top();

static_assert(LatA::leq(ε, short_));
static_assert(LatA::leq(ε, ω));
static_assert(LatA::leq(short_, mid));
static_assert(LatA::leq(mid, longish));
static_assert(LatA::leq(longish, ω));
static_assert(LatA::leq(ε, ε));
static_assert(LatA::leq(ω, ω));
static_assert(!LatA::leq(short_, ε));
static_assert(!LatA::leq(ω, longish));

static_assert(LatA::join(short_, mid) == mid);
static_assert(LatA::join(mid, short_) == mid);
static_assert(LatA::join(ε, ω) == ω);
static_assert(LatA::meet(short_, mid) == short_);
static_assert(LatA::meet(ε, ω) == ε);

// The witnesses cover the two boundaries, the interior, and the
// descending and mixed orderings of a triple.
static_assert(verify_bounded_lattice_axioms_at<LatA>(ε, ε, ε));
static_assert(verify_bounded_lattice_axioms_at<LatA>(ε, short_, mid));
static_assert(verify_bounded_lattice_axioms_at<LatA>(short_, mid, longish));
static_assert(verify_bounded_lattice_axioms_at<LatA>(mid, longish, ω));
static_assert(verify_bounded_lattice_axioms_at<LatA>(ω, ω, ω));
static_assert(verify_bounded_lattice_axioms_at<LatA>(longish, mid, short_));
static_assert(verify_bounded_lattice_axioms_at<LatA>(ε, mid, ω));
static_assert(verify_bounded_lattice_axioms_at<LatA>(ω, ε, longish));

constexpr Length<EventA> bumped_1 = LatA::join(ε, Length<EventA>{1});
constexpr Length<EventA> bumped_2 = LatA::join(bumped_1, Length<EventA>{2});
constexpr Length<EventA> bumped_3 = LatA::join(bumped_2, Length<EventA>{3});

static_assert(LatA::leq(ε, bumped_1));
static_assert(LatA::leq(bumped_1, bumped_2));
static_assert(LatA::leq(bumped_2, bumped_3));
static_assert(LatA::leq(ε, bumped_3));

static_assert(LatA::name() == "SeqPrefixLattice");
static_assert(LatB::name() == "SeqPrefixLattice");

static_assert(std::is_same_v<LatA::sequence_element_type, EventA>);
static_assert(std::is_same_v<LatB::sequence_element_type, EventB>);

// The grade is a size_t, so it cannot collapse under EBO the way an
// empty grade does.  Graded stores value and grade both, and the
// zero-overhead layout invariant deliberately does not apply here.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using AppendOnlyGraded = Graded<ModalityKind::Absolute, SeqPrefixLattice<EventA>, T>;

// The 7 is the padding that 8-byte alignment inserts after the 1-byte
// value.
static_assert(sizeof(AppendOnlyGraded<OneByteValue>) == sizeof(OneByteValue) + sizeof(LatA::element_type) + 7);

static_assert(sizeof(AppendOnlyGraded<EightByteValue>) == sizeof(EightByteValue) + sizeof(LatA::element_type));

inline void runtime_smoke_test() {
    std::size_t n_a = 5;
    std::size_t n_b = 17;
    Length<EventA> a{n_a};
    Length<EventA> b{n_b};

    [[maybe_unused]] bool l = LatA::leq(a, b);
    [[maybe_unused]] LatA::element_type j = LatA::join(a, b);
    [[maybe_unused]] LatA::element_type m = LatA::meet(a, b);
    [[maybe_unused]] LatA::element_type bot = LatA::bottom();
    [[maybe_unused]] LatA::element_type top = LatA::top();

    OneByteValue v{42};
    AppendOnlyGraded<OneByteValue> initial{v, LatA::bottom()};
    auto widened = initial.weaken(a);
    auto widened2 = widened.weaken(b);
    auto composed = initial.compose(widened2);
    auto rv_widen = std::move(widened2).weaken(b);
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto g2 = rv_widen.grade();
}

}  // namespace detail::seq_prefix_lattice_self_test

}  // namespace crucible::algebra::lattices
