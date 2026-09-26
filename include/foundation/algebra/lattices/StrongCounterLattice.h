#pragma once

// One bounded chain over a 64-bit counter, instantiated once per axis.
//
// Four axes of the old tree were the same lattice under four names: the
// fleet epoch, the node generation, the peak byte count and the bits
// budget.  Each is the numeric order on a 64-bit unsigned count, with
// zero at the bottom, the largest value at the top, the maximum as join
// and the minimum as meet.  This header states the lattice one time, and
// the tag parameter keeps the four axes apart: each instantiation has its
// own nested element type, so an epoch cannot be assigned from a
// generation, compared with one or joined with one.
//
// A count is an abstract type in the sense of the inductive naturals: it
// has a zero and a successor, and no other way in.  The doors are these:
//
//   - the default constructor and bottom(), the genesis count;
//   - successor(), one step up;
//   - saturating_sum(), the use of two stages in sequence, on a use axis
//     only, where a sum of two measured uses is itself a measured use;
//   - top(), the witness that a bounded lattice must have;
//   - mint_from_image(), the checked read of a count that crossed a wire
//     or came back from storage.
//
// No door takes an integer.  raw() gives the count back as a plain
// integer, and that integer builds no count of any axis, so an epoch
// cannot be restated as a generation through raw().  A requirement stated
// by a number is a bound_type, which a gate compares a count against and
// which never becomes a count.
//
// The image of a count names its axis.  mint_from_image() refuses an
// image of another axis, so a record written for the epoch does not read
// back as a generation.  The read needs a context that owns IO, the
// capability of a scope that reads storage or a socket, so a pure
// function or the foreground dispatch path cannot build a count from
// bytes.  A record that a program authors on purpose, under such a
// context, loads like any other: bytes read back from storage are
// whatever the storage holds.
//
// Two routes build an object from bytes without a constructor, and both
// are closed.  std::bit_cast needs a trivially copyable type, and the copy
// and move assignments here are user-provided, so a count is not one.
// The copy and move constructors stay trivial, so the Itanium ABI still
// passes a count in a register: the assignments do not decide how a call
// passes its argument.  That trivial constructor also makes a count an
// implicit-lifetime type, so the class carries the annotation that
// foundation::lifetime::start_as_array refuses, and a direct
// std::start_lifetime_as is refused by scripts/check-start-lifetime.py.
//
// The order knows nothing about which events happened.  A count is the
// number of successor steps in its own derivation.  Whether each step
// was a real membership change or a real restart is the property of the
// owner that takes the steps, which is fixy::VersionSource for a version.
//
// The order is the numeric one on all four axes, and that is not the
// same as the order of claims.  Graded treats its up direction as the
// weaker claim: weaken() and compose() move up and nowhere else.  For a
// use counter (peak bytes, bits) more use is the weaker claim, so these
// lattices grade a value correctly.  For a version counter (epoch,
// generation) the newer version is the stronger claim, so a Graded over
// the numeric order would let weaken() mark an old value as new.  Each
// tag states which of the two it is, the concept below refuses a tag
// that states neither, and Graded refuses a version counter in its
// numeric order at the template head.  A value graded by its version uses
// the order dual, as fixy/EpochVersioned.h does through DualLattice.h.
//
// Old spellings: include/crucible/algebra/lattices/{_EpochLattice,
// _GenerationLattice,_PeakBytesLattice,_BitsBudgetLattice}.h.

#include <foundation/Lifetime.h>
#include <foundation/Saturate.h>
#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

// A tag names one axis.  It is empty, so it adds no storage.  It
// publishes the name the lattice reports in a diagnostic, and the claim
// orientation of its count, which must be stated.
template <typename Tag>
concept CounterTag = std::is_empty_v<Tag> && requires {
    { Tag::lattice_name } -> std::convertible_to<std::string_view>;
    { Tag::claim_orientation } -> std::convertible_to<ClaimOrientation>;
} && (Tag::claim_orientation != ClaimOrientation::unstated);

// Why mint_from_image() refused an image.
enum class CountImageError : std::uint8_t {
    // The image names another axis, so it is a count of that axis and
    // not of this one.
    OtherAxis = 1,
};

namespace detail::count_image {

// Little-endian, whatever the host order, so an image moves between
// hosts unchanged.
template <std::size_t Offset, std::size_t Size>
constexpr void write_word(std::array<std::byte, Size>& image, std::uint64_t word) noexcept {
    static_assert(Offset + 8 <= Size, "the word must fit inside the image");
    for (std::size_t i = 0; i < 8; ++i) image[Offset + i] = static_cast<std::byte>((word >> (8 * i)) & 0xFFu);
}

template <std::size_t Offset, std::size_t Size>
[[nodiscard]] constexpr std::uint64_t read_word(std::array<std::byte, Size> const& image) noexcept {
    static_assert(Offset + 8 <= Size, "the word must fit inside the image");
    std::uint64_t word = 0;
    for (std::size_t i = 0; i < 8; ++i) word |= std::uint64_t{std::to_integer<std::uint8_t>(image[Offset + i])} << (8 * i);
    return word;
}

}  // namespace detail::count_image

template <CounterTag Tag>
struct StrongCounterLattice {
    // Graded reads this (ClaimOrientation.h).
    static constexpr ClaimOrientation claim_orientation = Tag::claim_orientation;

    // The count.  Nested in the template, so each tag gives a distinct type.
    class [[=::foundation::lifetime::no_start_over_bytes{}]] element_type {
    public:
        // The genesis count.
        constexpr element_type() noexcept = default;

        // Trivial, so that a call passes a count in a register.
        constexpr element_type(element_type const&) noexcept = default;
        constexpr element_type(element_type&&) noexcept = default;

        // User-provided, so that the type is not trivially copyable and
        // std::bit_cast does not build a count from bytes.  Each compiles
        // to one move of eight bytes.
        constexpr element_type& operator=(element_type const& other) noexcept {
            value_ = other.value_;
            return *this;
        }
        constexpr element_type& operator=(element_type&& other) noexcept {
            value_ = other.value_;
            return *this;
        }

        ~element_type() = default;

        [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value_; }

        [[nodiscard]] constexpr bool operator==(element_type const&) const noexcept = default;
        [[nodiscard]] constexpr auto operator<=>(element_type const&) const noexcept = default;

    private:
        friend struct StrongCounterLattice;
        constexpr explicit element_type(std::uint64_t count) noexcept : value_{count} {}

        std::uint64_t value_{0};
    };

    // A requirement: the count a gate asks for.  A number states one, and
    // it never becomes a count, so stating a bound claims nothing.
    class bound_type {
    public:
        constexpr explicit bound_type(std::uint64_t count) noexcept : value_{count} {}
        constexpr explicit bound_type(element_type count) noexcept : value_{count.raw()} {}

        [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value_; }

        [[nodiscard]] constexpr bool operator==(bound_type const&) const noexcept = default;
        [[nodiscard]] constexpr auto operator<=>(bound_type const&) const noexcept = default;

    private:
        std::uint64_t value_;
    };

    using tag_type = Tag;

    // The image of a count: eight bytes that name the axis, then eight
    // bytes of the count, each little-endian.
    using image_type = std::array<std::byte, 16>;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{}; }
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

    // The use of two stages that run one after the other.  A use axis only:
    // two versions have no sum.  The sum clamps at the top, which is the
    // weakest use claim, rather than wrap to a small count.
    [[nodiscard]] static constexpr element_type saturating_sum(element_type a, element_type b) noexcept
        requires(Tag::claim_orientation == ClaimOrientation::weaker_is_higher)
    {
        return element_type{::foundation::sat::add_sat(a.raw(), b.raw())};
    }

    // The admission queries: a count against a bound.
    [[nodiscard]] static constexpr bool is_at_least(element_type count, bound_type bound) noexcept {
        return count.raw() >= bound.raw();
    }
    [[nodiscard]] static constexpr bool is_at_most(element_type count, bound_type bound) noexcept {
        return count.raw() <= bound.raw();
    }

    // The identity of the axis in an image.  It is the stable id of the
    // lattice, so two axes have two identities and every translation unit
    // computes the same one.
    [[nodiscard]] static constexpr std::uint64_t image_axis() noexcept {
        return ::foundation::reflect::stable_type_id<StrongCounterLattice>;
    }

    [[nodiscard]] static constexpr image_type image_of(element_type count) noexcept {
        image_type image{};
        detail::count_image::write_word<0>(image, image_axis());
        detail::count_image::write_word<8>(image, count.raw());
        return image;
    }

    // The checked read of an image.  The context must own IO, so that the
    // read happens where storage or a socket is read, and the image must
    // name this axis.
    template <typename Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::IO>
    [[nodiscard]] static constexpr std::expected<element_type, CountImageError> mint_from_image(
        Ctx const&, image_type const& image) noexcept {
        if (detail::count_image::read_word<0>(image) != image_axis()) return std::unexpected(CountImageError::OtherAxis);
        return element_type{detail::count_image::read_word<8>(image)};
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
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;
};

// The generation of one node: the restart counter that the node
// advances by itself each time it comes back up.  It is local to one
// node, unlike the epoch it usually travels beside, and the two say
// different things about the same value.
struct generation {
    static constexpr std::string_view lattice_name = "GenerationLattice";
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;
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
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
};

// The count of bits transferred on the production path of a value.
// This order also runs by consumption.  A cap, where the smaller number
// is the stronger claim, is a different lattice with the reverse order.
// Folding the two readings into one would break each consumer silently,
// because the numbers stay the same.
struct bits_budget {
    static constexpr std::string_view lattice_name = "BitsBudgetLattice";
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
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

using EpochBound = EpochLattice::bound_type;
using GenerationBound = GenerationLattice::bound_type;
using PeakBytesBound = PeakBytesLattice::bound_type;
using BitsBudgetBound = BitsBudgetLattice::bound_type;

namespace detail::strong_counter_lattice_self_test {

// A count reached from genesis by `steps` successor steps: the only way
// a constant expression reaches an interior count.  Linear in steps.
template <typename L>
[[nodiscard]] consteval typename L::element_type after_steps(std::uint64_t steps) noexcept {
    typename L::element_type count = L::bottom();
    for (std::uint64_t i = 0; i < steps; ++i) count = L::successor(count);
    return count;
}

template <typename L>
[[nodiscard]] consteval bool laws_hold_for() noexcept {
    // The interior witnesses matter: bottom and top satisfy the
    // distributive law for reasons that have nothing to do with the
    // order between them.
    auto const c2 = after_steps<L>(2);
    auto const c5 = after_steps<L>(5);
    auto const c8 = after_steps<L>(8);
    auto const c42 = after_steps<L>(42);
    return verify_bounded_lattice_axioms_at<L>(L::bottom(), after_steps<L>(1024), L::top())
        && verify_bounded_lattice_axioms_at<L>(L::bottom(), c42, after_steps<L>(99))
        && verify_bounded_lattice_axioms_at<L>(after_steps<L>(1), c2, after_steps<L>(3))
        && verify_bounded_lattice_axioms_at<L>(c42, L::top(), L::bottom())
        && verify_distributive_lattice<L>(L::bottom(), after_steps<L>(1024), L::top())
        && verify_distributive_lattice<L>(c2, c5, c8) && verify_distributive_lattice<L>(c42, c42, c8)
        && verify_distributive_lattice<L>(c8, c2, c5);
}

template <typename L>
[[nodiscard]] consteval bool pins_hold_for() noexcept {
    constexpr std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    auto const c3 = after_steps<L>(3);
    auto const c7 = after_steps<L>(7);
    auto const c41 = after_steps<L>(41);
    return L::bottom().raw() == 0 && L::top().raw() == max && L::leq(L::bottom(), c3) && L::leq(c7, c7)
        && !L::leq(c7, c3) && L::join(c3, c7).raw() == 7 && L::join(c7, c3).raw() == 7 && L::meet(c3, c7).raw() == 3
        && L::meet(c7, c3).raw() == 3 && L::join(c41, L::bottom()) == c41 && L::meet(c41, L::top()) == c41
        && L::join(L::top(), c41) == L::top() && L::meet(L::bottom(), c41) == L::bottom()
        && typename L::element_type{} == L::bottom()
        // The successor is one step up and strictly above its input.
        && L::successor(L::bottom()).raw() == 1 && L::leq(c41, L::successor(c41)) && !(L::successor(c41) == c41)
        // A bound is compared, never joined: at least and at most meet at
        // the count itself.
        && L::is_at_least(c7, typename L::bound_type{7}) && !L::is_at_least(c7, typename L::bound_type{8})
        && L::is_at_most(c7, typename L::bound_type{7}) && !L::is_at_most(c7, typename L::bound_type{6})
        && L::is_at_least(c7, typename L::bound_type{c7}) && L::is_at_most(L::top(), typename L::bound_type{max});
}

// The image carries the axis and the count, and names the axis first.
template <typename L>
[[nodiscard]] consteval bool image_pins_hold_for() noexcept {
    auto const image = L::image_of(after_steps<L>(258));
    return detail::count_image::read_word<0>(image) == L::image_axis()
        && detail::count_image::read_word<8>(image) == 258 && image[8] == std::byte{2} && image[9] == std::byte{1};
}

// The count cannot be written through an element, and no integer builds
// one.
template <typename E>
concept count_is_writable = requires(E e) { e.raw() = 0; };

template <typename L>
[[nodiscard]] consteval bool shape_holds_for() noexcept {
    using E = typename L::element_type;
    using B = typename L::bound_type;
    return Lattice<L> && BoundedLattice<L> && !UnboundedLattice<L> && !Semiring<L>
        && sizeof(E) == sizeof(std::uint64_t) && std::is_standard_layout_v<E> && !std::is_same_v<E, std::uint64_t>
        && !std::is_convertible_v<E, std::uint64_t> && !std::is_convertible_v<std::uint64_t, E>
        && !std::is_constructible_v<E, std::uint64_t> && !std::is_constructible_v<E, int>
        && !std::is_constructible_v<E, B> && !std::is_convertible_v<B, E>
        // Not buildable from bytes, and still passed in a register.
        && !std::is_trivially_copyable_v<E> && std::is_trivially_copy_constructible_v<E>
        && std::is_trivially_move_constructible_v<E> && std::is_trivially_destructible_v<E>
        && !::foundation::lifetime::ImplicitLifetimeThroughout<E> && !count_is_writable<E>;
}

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

static_assert(image_pins_hold_for<EpochLattice>() && image_pins_hold_for<BitsBudgetLattice>());
static_assert(EpochLattice::image_axis() != GenerationLattice::image_axis()
                  && PeakBytesLattice::image_axis() != BitsBudgetLattice::image_axis()
                  && EpochLattice::image_axis() != PeakBytesLattice::image_axis(),
              "two axes share one image identity, so an image of one reads back as the other");

// The sum exists on a use axis and not on a version axis.
template <typename L>
concept has_sum = requires(typename L::element_type a) { L::saturating_sum(a, a); };
static_assert(has_sum<PeakBytesLattice> && has_sum<BitsBudgetLattice>);
static_assert(!has_sum<EpochLattice> && !has_sum<GenerationLattice>);
static_assert(PeakBytesLattice::saturating_sum(after_steps<PeakBytesLattice>(3), after_steps<PeakBytesLattice>(4)).raw()
              == 7);
static_assert(PeakBytesLattice::saturating_sum(PeakBytesLattice::top(), after_steps<PeakBytesLattice>(4))
                  == PeakBytesLattice::top(),
              "the sum clamps at the top");

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
static_assert(!mixes<EpochBound, GenerationBound>);

// The detector answers yes for a pair that does mix, so the assertions
// above cannot pass vacuously.
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
static_assert(sizeof(Graded<ModalityKind::Absolute, PeakBytesLattice, EightByteValue>) == 16);
static_assert(sizeof(Graded<ModalityKind::Absolute, BitsBudgetLattice, EightByteValue>) == 16);

// A use counter grades the Graded way.  A version counter in its numeric
// order does not, and Graded refuses it.
static_assert(GradableLattice<PeakBytesLattice> && GradableLattice<BitsBudgetLattice>);
static_assert(!GradableLattice<EpochLattice> && !GradableLattice<GenerationLattice>);

// A tag that states no orientation is not a counter tag.
struct silent_tag {
    static constexpr std::string_view lattice_name = "Silent";
};
struct unstated_tag {
    static constexpr std::string_view lattice_name = "Unstated";
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::unstated;
};
static_assert(!CounterTag<silent_tag> && !CounterTag<unstated_tag>);
static_assert(CounterTag<counter_tags::epoch> && CounterTag<counter_tags::bits_budget>);

}  // namespace detail::strong_counter_lattice_self_test

}  // namespace foundation::algebra::lattices
