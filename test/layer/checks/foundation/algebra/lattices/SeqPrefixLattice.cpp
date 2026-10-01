// The compile-time checks of foundation/algebra/lattices/SeqPrefixLattice.h.

#include <foundation/algebra/lattices/SeqPrefixLattice.h>

namespace foundation::algebra::lattices {

namespace detail::seq_prefix_lattice_self_test {

struct EventB {};

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

constexpr Length<EventA> empty_prefix = LatA::bottom();
constexpr Length<EventA> short_ = Length<EventA>{1};
constexpr Length<EventA> mid = Length<EventA>{1024};
constexpr Length<EventA> longish = Length<EventA>{1000000};
constexpr Length<EventA> ceiling = LatA::top();

static_assert(LatA::leq(empty_prefix, short_));
static_assert(LatA::leq(empty_prefix, ceiling));
static_assert(LatA::leq(short_, mid));
static_assert(LatA::leq(mid, longish));
static_assert(LatA::leq(longish, ceiling));
static_assert(LatA::leq(empty_prefix, empty_prefix));
static_assert(LatA::leq(ceiling, ceiling));
static_assert(!LatA::leq(short_, empty_prefix));
static_assert(!LatA::leq(ceiling, longish));

static_assert(LatA::join(short_, mid) == mid);
static_assert(LatA::join(mid, short_) == mid);
static_assert(LatA::join(empty_prefix, ceiling) == ceiling);
static_assert(LatA::meet(short_, mid) == short_);
static_assert(LatA::meet(empty_prefix, ceiling) == empty_prefix);

// The witnesses cover the two boundaries, the interior, and the
// descending and mixed orderings of a triple.
static_assert(verify_bounded_lattice_axioms_at<LatA>(empty_prefix, empty_prefix, empty_prefix));
static_assert(verify_bounded_lattice_axioms_at<LatA>(empty_prefix, short_, mid));
static_assert(verify_bounded_lattice_axioms_at<LatA>(short_, mid, longish));
static_assert(verify_bounded_lattice_axioms_at<LatA>(mid, longish, ceiling));
static_assert(verify_bounded_lattice_axioms_at<LatA>(ceiling, ceiling, ceiling));
static_assert(verify_bounded_lattice_axioms_at<LatA>(longish, mid, short_));
static_assert(verify_bounded_lattice_axioms_at<LatA>(empty_prefix, mid, ceiling));
static_assert(verify_bounded_lattice_axioms_at<LatA>(ceiling, empty_prefix, longish));

constexpr Length<EventA> bumped_1 = LatA::join(empty_prefix, Length<EventA>{1});
constexpr Length<EventA> bumped_2 = LatA::join(bumped_1, Length<EventA>{2});
constexpr Length<EventA> bumped_3 = LatA::join(bumped_2, Length<EventA>{3});

static_assert(LatA::leq(empty_prefix, bumped_1));
static_assert(LatA::leq(bumped_1, bumped_2));
static_assert(LatA::leq(bumped_2, bumped_3));
static_assert(LatA::leq(empty_prefix, bumped_3));

static_assert(LatA::name() == "SeqPrefixLattice");
static_assert(LatB::name() == "SeqPrefixLattice");

static_assert(std::is_same_v<LatA::sequence_element_type, EventA>);
static_assert(std::is_same_v<LatB::sequence_element_type, EventB>);

// An append-only container holds its own length.  Graded derives the
// prefix from it and stores nothing beside it.  A value that is not a
// sequence cannot carry a prefix as a stored grade, because a longer
// prefix is the stronger claim.
struct OneByteValue {
    char c{0};
};

static_assert(LatticeDerivesGrade<LatA, MiniLog>);
static_assert(sizeof(AppendOnlyGraded<MiniLog>) == sizeof(MiniLog));
static_assert(AppendOnlyGraded<MiniLog>{MiniLog{3}}.grade() == Length<EventA>{3});
static_assert(!GradableLattice<LatA> && !LatticeGradesValue<LatA, OneByteValue>);

}  // namespace detail::seq_prefix_lattice_self_test

}  // namespace foundation::algebra::lattices
