// The reset of a trace ring takes the two ends of one split.  The frames of two
// rings can nest, so a caller can give the producer end of one ring and the
// consumer end of another.  The reset would then move the counters of a ring
// whose consumer another thread can hold, so it ends the process.  The
// violation runs in a child process through expect_dies of the Check family.
// The build also gives this file the ignore semantic, where a CRUCIBLE_PRE is
// only an assumption, and the check holds there too.
//
// The frames of two metadata logs do not nest
// (neg_metalog_nested_frames_of_two_logs), so only the ends of one log reach
// its reset.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/SpscEnds.h>
#include <foundation/effects/Effect.h>

namespace {

using crucible::MetaLog;
using crucible::TraceRing;
using ::fixy::concurrent::mint_spsc_split;
using ::fixy::concurrent::with_consumer;
using ::fixy::concurrent::with_ends;
using ::fixy::concurrent::with_producer;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

void ends_of_two_rings_end_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            auto [first, first_producer, first_consumer] = mint_spsc_split<TraceRing>(kAlloc);
            auto [second, second_producer, second_consumer] = mint_spsc_split<TraceRing>(kAlloc);
            with_producer(first, first_producer, [&](TraceRing::Producer& producer) noexcept {
                with_consumer(second, second_consumer,
                              [&](TraceRing::Consumer& consumer) noexcept { TraceRing::reset(producer, consumer); });
            });
            static_cast<void>(first_consumer);
            static_cast<void>(second_producer);
        },
        ::fixy::TextView{"fatal invariant failed: ::fixy::same_object(producer.owner_, consumer.owner_)"});
}

// The two ends of one split reset it, in the frame of the two ends and, for a
// ring, in two frames that nest.
void ends_of_one_split_reset_it() {
    auto [ring, ring_producer, ring_consumer] = mint_spsc_split<TraceRing>(kAlloc);
    with_ends(ring, ring_producer, ring_consumer,
              [](TraceRing::Producer& producer, TraceRing::Consumer& consumer) noexcept {
                  TraceRing::reset(producer, consumer);
              });
    with_producer(ring, ring_producer, [&](TraceRing::Producer& producer) noexcept {
        with_consumer(ring, ring_consumer,
                      [&](TraceRing::Consumer& consumer) noexcept { TraceRing::reset(producer, consumer); });
    });
    auto [log, log_producer, log_consumer] = mint_spsc_split<MetaLog>(kAlloc, kAlloc);
    with_ends(log, log_producer, log_consumer, [](MetaLog::Producer& producer, MetaLog::Consumer& consumer) noexcept {
        MetaLog::reset(producer, consumer);
    });
}

}  // namespace

int main() {
    ends_of_one_split_reset_it();
    ends_of_two_rings_end_the_process();
    ::fixy::check::pass(kTest, "test_reset_of_two_ends: PASSED\n");
    return 0;
}
