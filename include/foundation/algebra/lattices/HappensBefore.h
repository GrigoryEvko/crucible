#pragma once

// The vector clock of Lamport 1978, Mattern 1988 and Fidge 1991, presented as
// the lattice of N-tuples of naturals under the pointwise order.
//
// This is the order that can say two events are concurrent.  A scalar clock
// is a chain, so of two events one always precedes the other.  Here two
// clocks that each lead on a different slot are unordered, and
// is_concurrent answers yes for them.
//
// A clock claims a history: the count in each slot is the number of events
// of that process that the clock has seen.  So a clock has the doors of an
// event history and no other.  bottom() is the empty history, successor_at()
// is a local event, causal_merge() is a receive, join() and meet() combine
// two clocks that exist, and top() is the witness a bounded lattice needs.
// mint_from_image() is the checked read of a clock that crossed a wire, and
// it needs a context that owns IO.  No door takes an array of integers, so
// a clock cannot claim a history nobody recorded.  The element is closed to
// std::bit_cast and to a lifetime start over bytes for the reasons given in
// StrongCounterLattice.h, and at the same cost, which is none.
//
// The image of a clock names its axis: the lattice kind, the source path
// of the tag and the width.  Each part is declared text or a number, and
// each toolchain and each host calculates the same word.  A tag with no
// source path, such as a class in an unnamed namespace, has no image door.
//
// A larger clock claims a longer history, and up is the stronger claim, as
// it is for a version counter.  A Graded over the pointwise order can let
// weaken() claim a history that no process recorded, and the doors above
// refuse that.  A value graded by its clock uses the order dual
// (DualLattice.h).
//
// The preconditions are CRUCIBLE_PRE in the function bodies, so they
// fire during constant evaluation too.

#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// A protocol whose clocks can cross a wire: the default protocol, a void
// tag, or a tag with a source path (StrongCounterLattice.h).
template <typename Tag>
concept ClockWireProtocol = std::is_void_v<Tag> || WireTag<Tag>;

// Tag has no data members and no effect on layout.  It exists so that clocks
// belonging to different protocols are different types and cannot be joined
// with one another by accident.
template <std::size_t N, typename Tag = void>
struct HappensBeforeLattice {
    static_assert(N > 0, "HappensBeforeLattice<0> is forbidden — an empty vector clock "
                         "has no algebraic content.  Use N >= 1; N=1 reduces to a "
                         "Lamport scalar clock.");

    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    // operator<=> is written out rather than defaulted.  A defaulted one would
    // forward to the array member and yield a lexicographic strong_ordering,
    // which is the wrong relation: two clocks that each lead on a different
    // slot are unordered, not one-before-the-other.  A non-strong ordering
    // also supplies no operator== of its own, so that one stays defaulted.
    // The slots are private, and only the lattice operations below produce
    // a clock, so a clock that exists cannot be edited in place and no
    // integer states one.
    class[[= ::foundation::lifetime::no_start_over_bytes{}]] element_type {
    public:
        // The empty history.
        constexpr element_type() noexcept = default;

        // Trivial copies keep the calling convention of an array of counts.
        // The assignments are user-provided, so std::bit_cast is refused.
        constexpr element_type(element_type const&) noexcept = default;
        constexpr element_type(element_type&&) noexcept = default;
        constexpr element_type& operator=(element_type const& other) noexcept {
            clock_ = other.clock_;
            return *this;
        }
        constexpr element_type& operator=(element_type&& other) noexcept {
            clock_ = other.clock_;
            return *this;
        }
        ~element_type() = default;

        [[nodiscard]] constexpr bool operator==(element_type const&) const noexcept = default;

        [[nodiscard]] constexpr std::partial_ordering operator<=>(element_type const& other) const noexcept {
            bool self_leq_other = true;
            bool other_leq_self = true;
            for (std::size_t i = 0; i < N; ++i) {
                if (clock_[i] > other.clock_[i]) self_leq_other = false;
                if (other.clock_[i] > clock_[i]) other_leq_self = false;
                if (!self_leq_other && !other_leq_self) break;
            }
            if (self_leq_other && other_leq_self) return std::partial_ordering::equivalent;
            if (self_leq_other) return std::partial_ordering::less;
            if (other_leq_self) return std::partial_ordering::greater;
            return std::partial_ordering::unordered;
        }

        // Read-only by design.  A clock advances only through successor_at and
        // causal_merge, so no accessor hands out a mutable slot.
        //
        // The bound is spelled `N - 1` and cannot underflow, because a zero N
        // is a hard build error above.
        [[nodiscard]] constexpr std::uint64_t operator[](std::size_t p) const noexcept {
            CRUCIBLE_PRE(::foundation::decide::in_range<std::size_t>(p, 0, N - 1));
            return clock_[p];
        }

        // A copy of every slot, so no reference into the clock escapes.
        [[nodiscard]] constexpr std::array<std::uint64_t, N> slots() const noexcept { return clock_; }

    private:
        friend struct HappensBeforeLattice;
        constexpr explicit element_type(std::array<std::uint64_t, N> const& slots) noexcept : clock_{slots} {}

        std::array<std::uint64_t, N> clock_{};
    };

    static constexpr std::size_t process_count = N;
    using process_id_type = std::size_t;
    using clock_value_type = std::uint64_t;
    using tag_type = Tag;

    // The image of a clock: eight bytes that name the lattice, then eight
    // bytes per slot, each little-endian.
    using image_type = std::array<std::byte, 8 * (N + 1)>;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{}; }

    // The order over the naturals has no greatest element, so this top is
    // synthesized rather than real.  A bounded lattice needs some witness, and
    // a clock that reached the maximum would already have overflowed.
    [[nodiscard]] static constexpr element_type top() noexcept {
        element_type result;
        for (std::size_t i = 0; i < N; ++i) {
            result.clock_[i] = std::numeric_limits<std::uint64_t>::max();
        }
        return result;
    }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (a.clock_[i] > b.clock_[i]) return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        element_type r;
        for (std::size_t i = 0; i < N; ++i) {
            r.clock_[i] = a.clock_[i] >= b.clock_[i] ? a.clock_[i] : b.clock_[i];
        }
        return r;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        element_type r;
        for (std::size_t i = 0; i < N; ++i) {
            r.clock_[i] = a.clock_[i] <= b.clock_[i] ? a.clock_[i] : b.clock_[i];
        }
        return r;
    }

    [[nodiscard]] static constexpr bool happens_before(element_type a, element_type b) noexcept {
        return leq(a, b) && !(a == b);
    }

    // Two clocks are concurrent when neither precedes the other.  This is what
    // a vector clock adds over a scalar one, whose order is total.  At N of one
    // the answer is always false.
    [[nodiscard]] static constexpr bool is_concurrent(element_type a, element_type b) noexcept {
        return !leq(a, b) && !leq(b, a);
    }

    [[nodiscard]] static constexpr bool comparable(element_type a, element_type b) noexcept {
        return leq(a, b) || leq(b, a);
    }

    // Models a local event at process p.  The second precondition is an
    // overflow guard: a slot that wrapped to zero would break monotonicity, so
    // the result would no longer be above its input.
    [[nodiscard]] static constexpr element_type successor_at(element_type v, std::size_t p) noexcept {
        CRUCIBLE_PRE(::foundation::decide::in_range<std::size_t>(p, 0, N - 1));
        CRUCIBLE_PRE(v.clock_[p] != std::numeric_limits<std::uint64_t>::max());
        v.clock_[p] += 1;
        return v;
    }

    // Models a receive event at process me: absorb everything the sender had
    // observed, then count the receive as a local event.
    //
    // Only the index bound is checked here.  The overflow guard belongs to
    // successor_at and is not repeated: the join is computed either way, and
    // its slot for me already equals the larger of the two inputs, so the
    // inner check sees the same value and there is one place to keep correct.
    [[nodiscard]] static constexpr element_type causal_merge(element_type local, element_type received,
                                                             std::size_t me) noexcept {
        CRUCIBLE_PRE(::foundation::decide::in_range<std::size_t>(me, 0, N - 1));
        return successor_at(join(local, received), me);
    }

    // The identity of the lattice in an image: the kind, the protocol and
    // the width.  A clock of another protocol or width does not read back
    // as this one.  The default protocol has the empty path, which no tag
    // can have.
    [[nodiscard]] static constexpr std::uint64_t image_axis() noexcept
        requires ClockWireProtocol<Tag>
    {
        return ::foundation::reflect::combine_ids(
            detail::count_image::wire_axis("HappensBeforeLattice", protocol_path_()), std::uint64_t{N});
    }

    [[nodiscard]] static constexpr image_type image_of(element_type clock) noexcept
        requires ClockWireProtocol<Tag>
    {
        image_type image{};
        detail::count_image::write_word(image, 0, image_axis());
        for (std::size_t p = 0; p < N; ++p)
            detail::count_image::write_word(image, 8 * (p + 1), clock.clock_[p]);
        return image;
    }

    // The checked read of an image.  The context must own IO, the tag must
    // have a source path, and the image must name this lattice.
    template <typename Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::IO>
              && ClockWireProtocol<Tag>
    [[nodiscard]] static constexpr std::expected<element_type, CountImageError>
    mint_from_image(Ctx const&, image_type const& image) noexcept {
        if (detail::count_image::read_word(image, 0) != image_axis())
            return std::unexpected(CountImageError::OtherAxis);
        std::array<std::uint64_t, N> slots{};
        for (std::size_t p = 0; p < N; ++p)
            slots[p] = detail::count_image::read_word(image, 8 * (p + 1));
        return element_type{slots};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "HappensBeforeLattice"; }

private:
    [[nodiscard]] static consteval std::string_view protocol_path_() noexcept
        requires ClockWireProtocol<Tag>
    {
        if constexpr (std::is_void_v<Tag>) {
            return {};
        } else {
            return detail::count_image::source_path(^^Tag);
        }
    }
};

}  // namespace foundation::algebra::lattices
