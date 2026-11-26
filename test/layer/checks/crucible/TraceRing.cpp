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

// Each counter owns its cache line and the arrays follow in order, so a
// wrapper change that moves a field shows here and not as a slowdown.  The
// members are private, so the check reads their offsets through reflection,
// in the order of their declaration: head_, tail_, entries_ and
// meta_starts_.  The members come as a constant array, so the loop reads no
// container.
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

static_assert(ring_offset_at(0) == 0 && ring_offset_at(1) == 64 && ring_offset_at(2) == 128
                  && ring_offset_at(3) == 128 + sizeof(TraceRing::Entry) * TraceRing::CAPACITY,
              "the counters sit on separate cache lines ahead of the arrays");

// The state of each side lives in its end, on a cache line of its own.  An
// end holds the Ref of its ring and then its state, so the state starts the
// second line of the end, and no line of the producer is a line of the
// consumer, of head_ or of tail_.
static_assert(alignof(TraceRing::producer_state) == 64 && sizeof(TraceRing::producer_state) == 64);
static_assert(alignof(TraceRing::consumer_state) == 64 && sizeof(TraceRing::consumer_state) == 64);
static_assert(sizeof(TraceRing::Producer) == 128 && alignof(TraceRing::Producer) == 64);
static_assert(sizeof(TraceRing::Consumer) == 128 && alignof(TraceRing::Consumer) == 64);

}  // namespace crucible
