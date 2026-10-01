#pragma once

// One bounded chain over a 64-bit counter, instantiated once per axis.
//
// Four axes are the same lattice under four names: the fleet epoch, the
// node generation, the peak byte count and the bits budget.  Each is the
// numeric order on a 64-bit unsigned count, with zero at the bottom, the
// largest value at the top, the maximum as join and the minimum as meet.
// This header states the lattice one time, and
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
// image of another axis, and a record written for the epoch does not read
// back as a generation.  The axis word comes from the source path of the
// tag: the declared identifiers of the tag and of each scope around it,
// hashed as bytes.  Each toolchain and each host writes and reads the
// same word.  A reflected display name changes with the spelling of the
// toolchain, and a record written by one build then does not read back
// in the next.  A tag with no source path, such as a class in an unnamed
// namespace, has no image door.  The read needs a
// context that owns IO, the capability of a scope that reads storage or a
// socket, so a pure function or the foreground dispatch path cannot build
// a count from bytes.  A record that a program authors on purpose, under such a
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
// std::start_lifetime_as is refused by utils/scripts/check-start-lifetime.py.
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

#include <foundation/ByteSeal.h>
#include <foundation/Saturate.h>
#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/contracts/Decide.h>
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
#include <meta>
#include <string>
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

// The one image codec of the counter and clock lattices.  A word is eight
// bytes, little-endian on each host, and an image moves between hosts with
// no change.  The offset is the byte position of the word, and a word that
// does not fit inside the image stops the build or the process.
template <std::size_t Size>
constexpr void write_word(std::array<std::byte, Size>& image, std::size_t offset, std::uint64_t word) noexcept {
    static_assert(Size >= 8, "an image holds at least one word");
    CRUCIBLE_PRE(::foundation::decide::in_range<std::size_t>(offset, 0, Size - 8));
    for (std::size_t i = 0; i < 8; ++i)
        image[offset + i] = static_cast<std::byte>((word >> (8 * i)) & 0xFFu);
}

template <std::size_t Size>
[[nodiscard]] constexpr std::uint64_t read_word(std::array<std::byte, Size> const& image, std::size_t offset) noexcept {
    static_assert(Size >= 8, "an image holds at least one word");
    CRUCIBLE_PRE(::foundation::decide::in_range<std::size_t>(offset, 0, Size - 8));
    std::uint64_t word = 0;
    for (std::size_t i = 0; i < 8; ++i)
        word |= std::uint64_t{std::to_integer<std::uint8_t>(image[offset + i])} << (8 * i);
    return word;
}

// A tag has a source path when each scope around it has a declared
// identifier and no template arguments.  A class in an unnamed namespace
// or in a function has none, and neither has a template specialization,
// whose arguments the path cannot carry.
[[nodiscard]] consteval bool has_source_path(std::meta::info entity) noexcept {
    if (!std::meta::has_identifier(entity) || std::meta::has_template_arguments(entity)) return false;
    for (std::meta::info scope = std::meta::parent_of(entity); scope != ^^::; scope = std::meta::parent_of(scope)) {
        if (!std::meta::is_namespace(scope) && !std::meta::is_type(scope)) return false;
        if (!std::meta::has_identifier(scope) || std::meta::has_template_arguments(scope)) return false;
    }
    return true;
}

// The source path of a tag, "a::b::Tag", from declared identifiers only.
// Each toolchain spells a declared identifier as the source does, but a
// reflected display name changes with the toolchain.  Two different tags
// have two paths.  The identity of a tag is its path, and its diagnostic
// name is not part of that identity.
[[nodiscard]] consteval std::string_view source_path(std::meta::info entity) {
    std::string path{std::meta::identifier_of(entity)};
    for (std::meta::info scope = std::meta::parent_of(entity); scope != ^^::; scope = std::meta::parent_of(scope)) {
        path.insert(0, "::");
        path.insert(0, std::meta::identifier_of(scope));
    }
    return std::define_static_string(path);
}

// The wire identity of one axis of one lattice kind.  FNV-1a with the
// fmix64 finalizer is unsigned arithmetic over bytes.  Each toolchain and
// each host calculates the same word from the same text.
[[nodiscard]] consteval std::uint64_t wire_axis(std::string_view kind, std::string_view axis) noexcept {
    return ::foundation::reflect::combine_ids(::foundation::reflect::detail::hash_name(kind),
                                              ::foundation::reflect::detail::hash_name(axis));
}

}  // namespace detail::count_image

// A tag whose identity can cross a wire: it has a source path.
template <typename Tag>
concept WireTag = detail::count_image::has_source_path(^^Tag);

template <CounterTag Tag>
struct StrongCounterLattice {
    // Graded reads this (ClaimOrientation.h).
    static constexpr ClaimOrientation claim_orientation = Tag::claim_orientation;

    // The count.  Nested in the template, so each tag gives a distinct type.
    class[[= ::foundation::lifetime::no_start_over_bytes{}]] element_type {
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

    // The identity of the axis in an image, from the source path of the
    // tag.  Two tags have two paths, and each toolchain calculates the same
    // word for one path.
    [[nodiscard]] static constexpr std::uint64_t image_axis() noexcept
        requires WireTag<Tag>
    {
        return detail::count_image::wire_axis("StrongCounterLattice", detail::count_image::source_path(^^Tag));
    }

    [[nodiscard]] static constexpr image_type image_of(element_type count) noexcept
        requires WireTag<Tag>
    {
        image_type image{};
        detail::count_image::write_word(image, 0, image_axis());
        detail::count_image::write_word(image, 8, count.raw());
        return image;
    }

    // The checked read of an image.  The context must own IO, because the
    // read occurs where storage or a socket is read.  The tag must have a
    // source path, and the image must name this axis.
    template <typename Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::IO> && WireTag<Tag>
    [[nodiscard]] static constexpr std::expected<element_type, CountImageError>
    mint_from_image(Ctx const&, image_type const& image) noexcept {
        if (detail::count_image::read_word(image, 0) != image_axis())
            return std::unexpected(CountImageError::OtherAxis);
        return element_type{detail::count_image::read_word(image, 8)};
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

}  // namespace foundation::algebra::lattices
