#pragma once

// One bounded chain over a 64-bit counter, instantiated once per axis.
//
// Four axes of the old tree were the same lattice under four names: the
// fleet epoch, the node generation, the peak byte count and the bits
// budget.  Each is the numeric order on a 64-bit unsigned count, with
// zero at the bottom, the largest value at the top, the maximum as join
// and the minimum as meet.  The old headers were identical apart from
// their names.  This header states the lattice one time, and the tag
// parameter keeps the four axes apart.
//
// The tag is the whole point of the template.  Each instantiation has
// its own nested element type, so an epoch cannot be assigned from a
// generation, compared with one or joined with one, although both hold
// the same integer.  A call site that swaps two axes does not compile.
//
// The element type has no implicit conversion to the integer.  The old
// types had one, and with it `epoch == generation` compiled, because
// both sides converted to std::uint64_t and the built-in comparison
// accepted them.  Read the count through raw().
//
// The count is private, and only two doors produce one.  The explicit
// constructor states a count, and successor() advances one by one step.
// So a count that exists cannot be edited in place, and a construction
// from an integer is a visible site in the source.  successor() refuses
// the largest count, because the step would wrap to zero and the result
// would sit below its input.
//
// The order knows nothing about history.  Nothing here stops a caller
// who builds an older count after a newer one, because the lattice sees
// two numbers and not a sequence of events.  Forward progress is a
// property of the source of the number, and a construction site must
// derive the number from that source rather than from an argument it
// was given.
//
// The order is the numeric one on all four axes, and that is not the
// same as the order of claims.  Graded treats its up direction as the
// weaker claim: weaken() and compose() move up and nowhere else.  For a
// use counter (peak bytes, bits) more use is the weaker claim, so these
// lattices grade a value correctly.  For a version counter (epoch,
// generation) the newer version is the stronger claim, so a Graded over
// the numeric order would let weaken() mark an old value as new.  A value
// graded by its version uses the order dual instead, as
// fixy/EpochVersioned.h does through DualLattice.h.
//
// Old spellings: include/crucible/algebra/lattices/{_EpochLattice,
// _GenerationLattice,_PeakBytesLattice,_BitsBudgetLattice}.h.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/contracts/Pre.h>

#include <compare>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

// A tag names one axis.  It is empty, so it adds no storage, and it
// publishes the name the lattice reports in a diagnostic.
template <typename Tag>
concept CounterTag = std::is_empty_v<Tag> && requires {
    { Tag::lattice_name } -> std::convertible_to<std::string_view>;
};

template <CounterTag Tag>
struct StrongCounterLattice {
    // Nested in the template, so each tag gives a distinct type.
    class element_type {
    public:
        constexpr element_type() noexcept = default;
        constexpr explicit element_type(std::uint64_t count) noexcept : value_{count} {}

        [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value_; }

        [[nodiscard]] constexpr bool operator==(element_type const&) const noexcept = default;
        [[nodiscard]] constexpr auto operator<=>(element_type const&) const noexcept = default;

    private:
        std::uint64_t value_{0};
    };

    using tag_type = Tag;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{0}; }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type{std::numeric_limits<std::uint64_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a.raw() <= b.raw(); }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return a.raw() >= b.raw() ? a : b;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return a.raw() <= b.raw() ? a : b;
    }

    // The next count.  The largest count has no successor: the step would
    // wrap to zero, so the result would sit below its input and a newer
    // epoch would read as the oldest one.
    [[nodiscard]] static constexpr element_type successor(element_type current) noexcept {
        CRUCIBLE_PRE(current.raw() != std::numeric_limits<std::uint64_t>::max());
        return element_type{current.raw() + 1};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return Tag::lattice_name; }
};

// ── The four axes ───────────────────────────────────────────────────

namespace counter_tags {

// The fleet epoch: the membership generation that the whole cluster
// commits through consensus.  It advances when a peer joins, when a
// peer is evicted, and when the fleet reshards.  A newer view is higher,
// and the join of two views is the more recent.
struct epoch {
    static constexpr std::string_view lattice_name = "EpochLattice";
};

// The generation of one node: the restart counter that the node
// advances by itself each time it comes back up.  It is local to one
// node, unlike the epoch it usually travels beside, and the two say
// different things about the same value.
struct generation {
    static constexpr std::string_view lattice_name = "GenerationLattice";
};

// The peak byte count resident while a value is produced.  The order
// runs by consumption, so the larger count is the higher element, and a
// gate that reads "held at most M bytes" admits exactly the values below
// its own grade.  The join is a maximum, not a sum: two peaks combine to
// the larger one only because the productions do not overlap in time.
// The peaks of concurrent producers add, and that calculation belongs to
// the memory planner, not to this order.
struct peak_bytes {
    static constexpr std::string_view lattice_name = "PeakBytesLattice";
};

// The count of bits transferred on the production path of a value.
// This order also runs by consumption.  A cap, where the smaller number
// is the stronger claim, is a different lattice with the reverse order.
// Folding the two readings into one would break each consumer silently,
// because the numbers stay the same.
struct bits_budget {
    static constexpr std::string_view lattice_name = "BitsBudgetLattice";
};

}  // namespace counter_tags

using EpochLattice = StrongCounterLattice<counter_tags::epoch>;
using GenerationLattice = StrongCounterLattice<counter_tags::generation>;
using PeakBytesLattice = StrongCounterLattice<counter_tags::peak_bytes>;
using BitsBudgetLattice = StrongCounterLattice<counter_tags::bits_budget>;

using Epoch = EpochLattice::element_type;
using Generation = GenerationLattice::element_type;
using PeakBytes = PeakBytesLattice::element_type;
using BitsBudget = BitsBudgetLattice::element_type;

namespace detail::strong_counter_lattice_self_test {

template <typename L>
[[nodiscard]] consteval bool laws_hold_for() noexcept {
    using E = typename L::element_type;
    constexpr std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    // The interior witnesses matter: bottom and top satisfy the
    // distributive law for reasons that have nothing to do with the
    // order between them.
    return verify_bounded_lattice_axioms_at<L>(L::bottom(), E{1024}, L::top())
        && verify_bounded_lattice_axioms_at<L>(E{0}, E{42}, E{99})
        && verify_bounded_lattice_axioms_at<L>(E{1}, E{2}, E{3})
        && verify_bounded_lattice_axioms_at<L>(E{max - 1}, E{max}, E{0})
        && verify_distributive_lattice<L>(L::bottom(), E{1024}, L::top())
        && verify_distributive_lattice<L>(E{2}, E{5}, E{8})
        && verify_distributive_lattice<L>(E{7}, E{7}, E{42})
        && verify_distributive_lattice<L>(E{8}, E{2}, E{5});
}

template <typename L>
[[nodiscard]] consteval bool pins_hold_for() noexcept {
    using E = typename L::element_type;
    constexpr std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    return L::bottom().raw() == 0 && L::top().raw() == max && L::leq(E{0}, E{1}) && L::leq(E{42}, E{42})
        && !L::leq(E{99}, E{42}) && L::leq(L::bottom(), L::top()) && L::join(E{3}, E{7}).raw() == 7
        && L::join(E{7}, E{3}).raw() == 7 && L::meet(E{3}, E{7}).raw() == 3 && L::meet(E{7}, E{3}).raw() == 3
        && L::join(E{42}, L::bottom()) == E{42} && L::meet(E{42}, L::top()) == E{42}
        && L::join(L::top(), E{42}) == L::top() && L::meet(L::bottom(), E{42}) == L::bottom() && E{} == L::bottom()
        // The successor is one step up and strictly above its input, up to
        // the count one below the top.
        && L::successor(L::bottom()) == E{1} && L::successor(E{max - 1}) == L::top()
        && L::leq(E{41}, L::successor(E{41})) && !(L::successor(E{41}) == E{41});
}

// The count cannot be written through an element.  Only the explicit
// constructor and successor() produce one.
template <typename E>
concept count_is_writable = requires(E e) { e.raw() = 0; };

template <typename L>
[[nodiscard]] consteval bool shape_holds_for() noexcept {
    using E = typename L::element_type;
    return Lattice<L> && BoundedLattice<L> && !UnboundedLattice<L> && !Semiring<L>
        && sizeof(E) == sizeof(std::uint64_t) && std::is_trivially_copyable_v<E> && std::is_standard_layout_v<E>
        && !std::is_same_v<E, std::uint64_t> && !std::is_convertible_v<E, std::uint64_t>
        && !std::is_convertible_v<std::uint64_t, E>;
}

static_assert(!count_is_writable<Epoch>);
static_assert(std::is_constructible_v<Epoch, std::uint64_t>, "the explicit constructor is the door");

static_assert(shape_holds_for<EpochLattice>());
static_assert(shape_holds_for<GenerationLattice>());
static_assert(shape_holds_for<PeakBytesLattice>());
static_assert(shape_holds_for<BitsBudgetLattice>());

static_assert(pins_hold_for<EpochLattice>());
static_assert(pins_hold_for<GenerationLattice>());
static_assert(pins_hold_for<PeakBytesLattice>());
static_assert(pins_hold_for<BitsBudgetLattice>());

static_assert(laws_hold_for<EpochLattice>());
static_assert(laws_hold_for<GenerationLattice>());
static_assert(laws_hold_for<PeakBytesLattice>());
static_assert(laws_hold_for<BitsBudgetLattice>());

// The four axes are four types, and no two of them mix.  The pairs
// below cover each unordered pair once.
template <typename A, typename B>
concept mixes = std::is_convertible_v<A, B> || std::is_convertible_v<B, A> || requires(A a, B b) { a == b; };

static_assert(!std::is_same_v<Epoch, Generation>);
static_assert(!std::is_same_v<PeakBytes, BitsBudget>);
static_assert(!mixes<Epoch, Generation>);
static_assert(!mixes<Epoch, PeakBytes>);
static_assert(!mixes<Epoch, BitsBudget>);
static_assert(!mixes<Generation, PeakBytes>);
static_assert(!mixes<Generation, BitsBudget>);
static_assert(!mixes<PeakBytes, BitsBudget>);

// The detector answers yes for a pair that does mix, so the eight
// assertions above cannot pass vacuously.
static_assert(mixes<Epoch, Epoch>);

static_assert(EpochLattice::name() == "EpochLattice");
static_assert(GenerationLattice::name() == "GenerationLattice");
static_assert(PeakBytesLattice::name() == "PeakBytesLattice");
static_assert(BitsBudgetLattice::name() == "BitsBudgetLattice");

// The grade is carried per instance, so a carrier over one axis pays
// the eight bytes of the counter.
struct EightByteValue {
    unsigned long long v{0};
};
static_assert(sizeof(Graded<ModalityKind::Absolute, EpochLattice, EightByteValue>) == 16);
static_assert(sizeof(Graded<ModalityKind::Absolute, BitsBudgetLattice, EightByteValue>) == 16);

}  // namespace detail::strong_counter_lattice_self_test

}  // namespace foundation::algebra::lattices
