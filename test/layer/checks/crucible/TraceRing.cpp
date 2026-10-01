// The compile-time checks of crucible/TraceRing.h.

#include <crucible/TraceRing.h>

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
// wrapper change that moves a field shows here and not as a slowdown.
static_assert(std::is_standard_layout_v<TraceRing>);
static_assert(offsetof(TraceRing, head) == 0 && offsetof(TraceRing, tail) == 64
                  && offsetof(TraceRing, consumer_tail_) == 128 && offsetof(TraceRing, cached_tail_) == 192
                  && offsetof(TraceRing, entries) == 256
                  && offsetof(TraceRing, meta_starts) == 256 + sizeof(TraceRing::Entry) * TraceRing::CAPACITY,
              "the counters sit on separate cache lines ahead of the arrays");

}  // namespace crucible
