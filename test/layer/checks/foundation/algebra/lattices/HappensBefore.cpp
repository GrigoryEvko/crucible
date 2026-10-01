// The compile-time checks of foundation/algebra/lattices/HappensBefore.h.

#include <foundation/algebra/lattices/HappensBefore.h>

namespace foundation::algebra::lattices {

namespace detail::happens_before_self_test {

// A clock reached from the empty history by steps[p] local events at each
// process p: the only way a constant expression reaches an interior clock.
// Linear in the sum of the steps.
template <typename HB>
[[nodiscard]] consteval typename HB::element_type
clock_after(std::array<std::uint64_t, HB::process_count> const& steps) noexcept {
    typename HB::element_type clock = HB::bottom();
    for (std::size_t p = 0; p < HB::process_count; ++p) {
        for (std::uint64_t i = 0; i < steps[p]; ++i)
            clock = HB::successor_at(clock, p);
    }
    return clock;
}

using HB4 = HappensBeforeLattice<4>;

static_assert(Lattice<HB4>);
static_assert(BoundedLattice<HB4>);
static_assert(BoundedBelowLattice<HB4>);
static_assert(BoundedAboveLattice<HB4>);

// The negative assertions pin what this lattice is not, so that dropping
// bottom or top silently demotes nothing, and so that no carrier can be
// instantiated over it expecting a multiplicative structure it does not have.
static_assert(!UnboundedLattice<HB4>);
static_assert(!Semiring<HB4>);

static_assert(!std::is_empty_v<HB4::element_type>);
static_assert(sizeof(HB4::element_type) == sizeof(std::uint64_t) * 4);
static_assert(HB4::process_count == 4);

static_assert(std::three_way_comparable<HB4::element_type, std::partial_ordering>);

inline constexpr HB4::element_type hb4_bot{};
inline constexpr HB4::element_type hb4_top = HB4::top();

// A chain: hb4_a precedes hb4_b precedes hb4_c.
inline constexpr HB4::element_type hb4_a = clock_after<HB4>({1, 0, 0, 0});
inline constexpr HB4::element_type hb4_b = clock_after<HB4>({1, 1, 0, 0});
inline constexpr HB4::element_type hb4_c = clock_after<HB4>({2, 2, 1, 0});

// A concurrent pair.
inline constexpr HB4::element_type hb4_x = clock_after<HB4>({2, 0, 1, 0});
inline constexpr HB4::element_type hb4_y = clock_after<HB4>({0, 2, 0, 1});

// The slots of a clock, for pins against a literal.
using Slots4 = std::array<std::uint64_t, 4>;

static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_bot, hb4_bot, hb4_bot));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_bot, hb4_a, hb4_top));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_a, hb4_b, hb4_c));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_x, hb4_y, hb4_top));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_a, hb4_x, hb4_y));

// Each slot is a chain, and a product of distributive lattices is
// distributive, so the pointwise order distributes.
static_assert(verify_distributive_lattice<HB4>(hb4_a, hb4_b, hb4_c));
static_assert(verify_distributive_lattice<HB4>(hb4_x, hb4_y, hb4_a));
static_assert(verify_distributive_lattice<HB4>(hb4_bot, hb4_top, hb4_x));
static_assert(verify_distributive_lattice<HB4>(hb4_a, hb4_a, hb4_y));

static_assert(HB4::leq(hb4_a, hb4_b));
static_assert(HB4::leq(hb4_b, hb4_c));
static_assert(HB4::leq(hb4_a, hb4_c));
static_assert(HB4::happens_before(hb4_a, hb4_b));
static_assert(HB4::happens_before(hb4_b, hb4_c));
static_assert(HB4::happens_before(hb4_a, hb4_c));
static_assert(!HB4::is_concurrent(hb4_a, hb4_b));
static_assert(!HB4::is_concurrent(hb4_a, hb4_c));
static_assert(HB4::comparable(hb4_a, hb4_b));
static_assert(HB4::comparable(hb4_a, hb4_c));

static_assert(!HB4::leq(hb4_b, hb4_a));
static_assert(!HB4::happens_before(hb4_b, hb4_a));

static_assert(HB4::leq(hb4_a, hb4_a));
static_assert(!HB4::happens_before(hb4_a, hb4_a));
static_assert(!HB4::is_concurrent(hb4_a, hb4_a));

// The axiom rollups above already prove the bottom and top identities, but
// they prove them algebraically.  These restate the same facts as order
// questions, which is what catches a leq whose arguments got swapped while the
// identities still hold.
static_assert(HB4::leq(hb4_bot, hb4_a));
static_assert(HB4::leq(hb4_bot, hb4_b));
static_assert(HB4::leq(hb4_bot, hb4_c));
static_assert(HB4::leq(hb4_bot, hb4_x));
static_assert(HB4::leq(hb4_bot, hb4_y));
static_assert(HB4::leq(hb4_bot, hb4_top));
static_assert(HB4::leq(hb4_a, hb4_top));
static_assert(HB4::leq(hb4_b, hb4_top));
static_assert(HB4::leq(hb4_c, hb4_top));
static_assert(HB4::leq(hb4_x, hb4_top));
static_assert(HB4::leq(hb4_y, hb4_top));

// Pinning the two values as well catches a top that returned the zero vector,
// which the algebraic rollups would still accept.
static_assert(hb4_bot.slots() == Slots4{0, 0, 0, 0});
static_assert(hb4_top.slots()
              == Slots4{std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max(),
                        std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max()});

// All four results of the comparison must be reachable.
static_assert((hb4_bot <=> hb4_bot) == std::partial_ordering::equivalent);
static_assert((hb4_a <=> hb4_a) == std::partial_ordering::equivalent);
static_assert((hb4_a <=> hb4_b) == std::partial_ordering::less);
static_assert((hb4_b <=> hb4_a) == std::partial_ordering::greater);
static_assert((hb4_a <=> hb4_c) == std::partial_ordering::less);
static_assert((hb4_x <=> hb4_y) == std::partial_ordering::unordered);
static_assert((hb4_y <=> hb4_x) == std::partial_ordering::unordered);
static_assert((hb4_bot <=> hb4_top) == std::partial_ordering::less);
static_assert((hb4_top <=> hb4_bot) == std::partial_ordering::greater);

static_assert(hb4_a < hb4_b);
static_assert(hb4_a <= hb4_b);
static_assert(hb4_b > hb4_a);
static_assert(hb4_a == hb4_a);
static_assert(!(hb4_x < hb4_y));
static_assert(!(hb4_x > hb4_y));
static_assert(!(hb4_x == hb4_y));
static_assert(!(hb4_x <= hb4_y));
static_assert(!(hb4_y <= hb4_x));

// The chain and the concurrent pair are not disjoint.  Pinning where they
// meet keeps a rewiring of the witnesses from quietly changing what the
// assertions above test.
static_assert(HB4::leq(hb4_a, hb4_x));
static_assert(HB4::happens_before(hb4_a, hb4_x));
static_assert(!HB4::is_concurrent(hb4_a, hb4_x));

static_assert(HB4::is_concurrent(hb4_a, hb4_y));
static_assert(!HB4::leq(hb4_a, hb4_y));
static_assert(!HB4::leq(hb4_y, hb4_a));

static_assert(HB4::is_concurrent(hb4_b, hb4_y));

// hb4_x is concurrent with hb4_y and still strictly precedes hb4_c, so the
// order has an antichain sitting inside a chain.
static_assert(HB4::leq(hb4_x, hb4_c));
static_assert(HB4::happens_before(hb4_x, hb4_c));
static_assert(!HB4::is_concurrent(hb4_x, hb4_c));

static_assert(!HB4::leq(hb4_x, hb4_y));
static_assert(!HB4::leq(hb4_y, hb4_x));
static_assert(HB4::is_concurrent(hb4_x, hb4_y));
static_assert(HB4::is_concurrent(hb4_y, hb4_x));
static_assert(!HB4::happens_before(hb4_x, hb4_y));
static_assert(!HB4::happens_before(hb4_y, hb4_x));
static_assert(!HB4::comparable(hb4_x, hb4_y));

static_assert(HB4::leq(hb4_x, HB4::join(hb4_x, hb4_y)));
static_assert(HB4::leq(hb4_y, HB4::join(hb4_x, hb4_y)));
static_assert(HB4::join(hb4_x, hb4_y).slots() == Slots4{2, 2, 1, 1});

// The meet of two clocks is their latest common ancestor.
static_assert(HB4::meet(hb4_x, hb4_y).slots() == Slots4{0, 0, 0, 0});

inline constexpr HB4::element_type hb4_a_after_p0 = HB4::successor_at(hb4_a, 0);
static_assert(hb4_a_after_p0.slots() == Slots4{2, 0, 0, 0});
static_assert(HB4::leq(hb4_a, hb4_a_after_p0));
static_assert(HB4::happens_before(hb4_a, hb4_a_after_p0));
static_assert(!HB4::leq(hb4_a_after_p0, hb4_a));

inline constexpr HB4::element_type hb4_a_after_p2 = HB4::successor_at(hb4_a, 2);
static_assert(hb4_a_after_p2.slots() == Slots4{1, 0, 1, 0});
static_assert(HB4::leq(hb4_a, hb4_a_after_p2));

// Local events at two processes with no message between them are concurrent.
static_assert(HB4::is_concurrent(hb4_a_after_p0, hb4_a_after_p2));

inline constexpr HB4::element_type hb4_received_y = HB4::causal_merge(hb4_a, hb4_y, 0);
static_assert(hb4_received_y.slots() == Slots4{2, 2, 0, 1});

static_assert(HB4::leq(hb4_a, hb4_received_y));
static_assert(HB4::leq(hb4_y, hb4_received_y));

static_assert(HB4::happens_before(hb4_a, hb4_received_y));
static_assert(HB4::happens_before(hb4_y, hb4_received_y));

// The slot reader sits inside the checked bound at both ends.
static_assert(hb4_c[0] == 2);
static_assert(hb4_c[3] == 0);

// A single participant is the boundary case that catches any accidental
// assumption of two or more slots.  Its order is total, so nothing is
// concurrent with anything.
using HB1 = HappensBeforeLattice<1>;
inline constexpr HB1::element_type hb1_zero = HB1::bottom();
inline constexpr HB1::element_type hb1_one = clock_after<HB1>({1});
inline constexpr HB1::element_type hb1_two = clock_after<HB1>({2});

static_assert(HB1::leq(hb1_zero, hb1_one));
static_assert(HB1::leq(hb1_one, hb1_two));
static_assert(HB1::happens_before(hb1_zero, hb1_two));
static_assert(!HB1::is_concurrent(hb1_zero, hb1_one));
static_assert(!HB1::is_concurrent(hb1_one, hb1_two));
static_assert(HB1::comparable(hb1_zero, hb1_two));

static_assert(verify_bounded_lattice_axioms_at<HB1>(hb1_zero, hb1_one, hb1_two));
static_assert(verify_distributive_lattice<HB1>(hb1_zero, hb1_one, hb1_two));

inline constexpr HB1::element_type hb1_zero_after_succ = HB1::successor_at(hb1_zero, 0);
static_assert(hb1_zero_after_succ.slots() == std::array<std::uint64_t, 1>{1});
static_assert(hb1_zero_after_succ == hb1_one);
static_assert(HB1::happens_before(hb1_zero, hb1_zero_after_succ));

inline constexpr HB1::element_type hb1_merged = HB1::causal_merge(hb1_one, hb1_two, 0);
static_assert(hb1_merged.slots() == std::array<std::uint64_t, 1>{3});
static_assert(HB1::leq(hb1_one, hb1_merged));
static_assert(HB1::leq(hb1_two, hb1_merged));
static_assert(HB1::happens_before(hb1_one, hb1_merged));
static_assert(HB1::happens_before(hb1_two, hb1_merged));

// The unordered result needs one slot where each side leads, which a single
// slot cannot supply, so no comparison here can reach it.
static_assert((hb1_zero <=> hb1_one) == std::partial_ordering::less);
static_assert((hb1_two <=> hb1_one) == std::partial_ordering::greater);
static_assert((hb1_one <=> hb1_one) == std::partial_ordering::equivalent);

struct ReplayClockTag {};
struct KernelOrderClockTag {};

using HBReplay = HappensBeforeLattice<4, ReplayClockTag>;
using HBKernel = HappensBeforeLattice<4, KernelOrderClockTag>;
using HBDefault = HappensBeforeLattice<4>;

static_assert(!std::is_same_v<HBReplay, HBKernel>);
static_assert(!std::is_same_v<HBReplay, HBDefault>);
static_assert(!std::is_same_v<HBKernel, HBDefault>);
static_assert(!std::is_same_v<typename HBReplay::element_type, typename HBKernel::element_type>);

// The tag changes the type without changing the storage.
static_assert(sizeof(HBReplay::element_type) == sizeof(HBKernel::element_type));
static_assert(sizeof(HBReplay::element_type) == sizeof(HBDefault::element_type));

static_assert(HB4::name() == "HappensBeforeLattice");
static_assert(HBReplay::name() == "HappensBeforeLattice");

// No integer and no array of integers states a clock, and no byte image
// becomes one without the checked read.  The clock is closed to bit_cast
// and to a lifetime start over bytes, and still copies trivially.
static_assert(!std::is_constructible_v<HB4::element_type, Slots4>);
static_assert(!std::is_constructible_v<HB1::element_type, std::uint64_t>);
static_assert(!std::is_convertible_v<Slots4, HB4::element_type>);
static_assert(!std::is_trivially_copyable_v<HB4::element_type>
              && std::is_trivially_copy_constructible_v<HB4::element_type>
              && !::foundation::lifetime::ImplicitLifetimeThroughout<HB4::element_type>);

// The image names the lattice first, then each slot, and two protocols or
// two widths have two identities.
[[nodiscard]] consteval bool image_pins_hold() noexcept {
    auto const image = HB4::image_of(hb4_c);
    return image.size() == 40 && image[8] == std::byte{2} && image[16] == std::byte{2} && image[24] == std::byte{1}
        && image[32] == std::byte{0};
}
static_assert(image_pins_hold());
static_assert(HBReplay::image_axis() != HBKernel::image_axis() && HBReplay::image_axis() != HBDefault::image_axis()
                  && HB4::image_axis() != HB1::image_axis(),
              "two clock lattices share one image identity");

// The axis words are a wire format.  They depend on declared identifiers
// and on the width only, and these values hold on each toolchain.  A
// renamed tag or a changed fold fails here, before the read of a stored
// clock fails.
static_assert(HB1::image_axis() == 0x2ec9a8810be113dfULL);
static_assert(HB4::image_axis() == 0xe01653134ef10969ULL);
static_assert(HBReplay::image_axis() == 0x22761349051f2ad2ULL);

// A one-slot clock and a count have images of one size.  The kind is part
// of the axis word, and a clock over the tag of a counter axis does not
// read back as a count of that axis.
static_assert(sizeof(HappensBeforeLattice<1, counter_tags::epoch>::image_type) == sizeof(EpochLattice::image_type));
static_assert(HappensBeforeLattice<1, counter_tags::epoch>::image_axis() != EpochLattice::image_axis());

// A template specialization has no source path, and a clock over it has
// no image door.  The test of a tag in an unnamed namespace is the fixture
// neg_happens_before_image_without_source_path, because a header holds no
// unnamed namespace.
template <int Width>
struct TemplatedClockTag {};
template <typename HB>
concept has_image_door = requires(typename HB::element_type clock) { HB::image_of(clock); };
static_assert(has_image_door<HB4> && has_image_door<HBReplay>);
static_assert(!has_image_door<HappensBeforeLattice<4, TemplatedClockTag<1>>>);

// The slots of a clock cannot be written through the clock: the reader
// returns a copy, and the storage is private.
static_assert(!std::is_reference_v<decltype(std::declval<HB4::element_type const&>().slots())>);
static_assert(std::is_same_v<decltype(hb4_c.slots()), std::array<std::uint64_t, 4>>);
static_assert(hb4_c.slots()[1] == 2);

// A clock in its pointwise order is not a stored grade, and its dual is.
static_assert(!GradableLattice<HB4> && !GradableLattice<HBReplay>);
static_assert(claim_orientation_v<HB4> == ClaimOrientation::stronger_is_higher);

}  // namespace detail::happens_before_self_test

}  // namespace foundation::algebra::lattices
