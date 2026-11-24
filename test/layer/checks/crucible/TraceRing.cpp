// The compile-time checks of crucible/TraceRing.h.

#include <crucible/TraceRing.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace crucible {

namespace tracering_hw {

static_assert(kPrefetchLocality >= 0 && kPrefetchLocality <= 3, "prefetch locality must be in [0, 3], the "
                                                                "__builtin_prefetch third-argument domain");

static_assert(::fixy::atom::IsAtom<InstructionTier> && InstructionTier::axis == ::fixy::Axis::HwInstruction,
              "the ring's instruction tier is a shipped atom of the HwInstruction axis");
static_assert(!::fixy::atom::hw::at_or_above(InstructionTier::tier,
                                             ::fixy::atom::hw::HwInstruction::NonDeterministicTsc),
              "the append runs on the hot path, which refuses the timestamp and privileged tiers");

}  // namespace tracering_hw

static_assert(sizeof(TraceRing) >= (5u * 1024u * 1024u) && sizeof(TraceRing) <= (6u * 1024u * 1024u),
              "TraceRing footprint must stay inside the 5-6 MB envelope");

// Each counter owns its cache line and the four arrays follow in order, so a
// wrapper change that moves a field shows here and not as a slowdown.  The
// members are private, so the check reads their offsets through reflection,
// in the order of their declaration: head_, tail_, consumer_tail_,
// cached_tail_, own_head_, entries_ and meta_starts_.  The members come as a
// constant array, so the loop reads no container.
static_assert(std::is_standard_layout_v<TraceRing>);

template <std::meta::info Type>
inline constexpr std::meta::info ring_members_ = std::meta::reflect_constant_array(
    std::meta::nonstatic_data_members_of(Type, std::meta::access_context::unchecked()));

[[nodiscard]] consteval std::size_t ring_offset_at(std::size_t position) {
    std::size_t index = 0;
    for (std::meta::info const member : [:ring_members_<^^TraceRing>:]) {
        if (index == position) return static_cast<std::size_t>(std::meta::offset_of(member).bytes);
        ++index;
    }
    return ~std::size_t{0};
}

static_assert(ring_offset_at(0) == 0 && ring_offset_at(1) == 64 && ring_offset_at(2) == 128 && ring_offset_at(3) == 192
                  && ring_offset_at(5) == 256
                  && ring_offset_at(6) == 256 + sizeof(TraceRing::Entry) * TraceRing::CAPACITY,
              "the counters sit on separate cache lines ahead of the arrays");
// The producer reads its position and the cache of the tail from one line,
// which only the producer touches (the two publishes of crucible/TraceRing.h).
static_assert(ring_offset_at(4) / 64 == ring_offset_at(3) / 64,
              "the copy of the head shares the private line of the producer with the cache of the tail");

// The ends of a ring hold no state: the ring keeps the line of each side.
static_assert(std::is_empty_v<TraceRing::producer_state> && std::is_empty_v<TraceRing::consumer_state>);

// A reservation only moves, and its moves are those of its bytes.  An Option
// of a reservation moves it through the owning slot.
using HeldReservation = ::fixy::Option<TraceRing::Reservation>;
static_assert(__is_trivially_copyable(TraceRing::Reservation));
static_assert(!__is_constructible(TraceRing::Reservation, TraceRing::Reservation const&));
static_assert(__is_nothrow_constructible(HeldReservation, HeldReservation&&));
static_assert(__is_nothrow_assignable(HeldReservation&, HeldReservation&&));

}  // namespace crucible
