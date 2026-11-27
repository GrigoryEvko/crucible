// The reset of a trace ring and of a metadata log takes the pair of the two
// ends of one split.  Only a pair door builds a pair, so two frames of two
// rings give no pair (neg_tracering_reset_of_two_rings).  The pair door
// compares the Identity of each token with the split, so two tokens of two
// splits of one mint site give no pair: the door ends the process.  The
// violation runs in a child process through expect_dies of the Check family.
// The build also gives this file the ignore semantic, where a CRUCIBLE_PRE is
// only an assumption, and the check holds there too.

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
using ::fixy::concurrent::with_end_pair;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// Runs body with two rings of one mint site that are alive at one time, and
// their tokens.  The template calls itself one time, so its two
// instantiations run one call of the mint, and the two splits have one type.
template <class Body, class... First>
void with_two_rings(Body& body, First&... first) noexcept {
    auto [split, producer_token, consumer_token] = mint_spsc_split<TraceRing>(kAlloc);
    if constexpr (sizeof...(First) == 0) {
        with_two_rings(body, split, producer_token, consumer_token);
    } else {
        body(first..., split, producer_token, consumer_token);
    }
}

void tokens_of_two_rings_end_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            auto body = [](auto& first_split, auto& first_producer, auto& first_consumer, auto& split, auto& producer,
                           auto& consumer) noexcept {
                static_cast<void>(first_split);
                static_cast<void>(consumer);
                with_end_pair(split, producer, first_consumer,
                              [](TraceRing::EndPair& ends) noexcept { TraceRing::reset(ends); });
                static_cast<void>(first_producer);
            };
            with_two_rings(body);
        },
        ::fixy::TextView{"fatal invariant failed: token.split_.names(identity_)"});
}

// The pair of the two ends of one split resets it.
void pair_of_one_split_resets_it() {
    auto [ring, ring_producer, ring_consumer] = mint_spsc_split<TraceRing>(kAlloc);
    with_end_pair(ring, ring_producer, ring_consumer,
                  [](TraceRing::EndPair& ends) noexcept { TraceRing::reset(ends); });
    auto [log, log_producer, log_consumer] = mint_spsc_split<MetaLog>(kAlloc, kAlloc);
    with_end_pair(log, log_producer, log_consumer, [](MetaLog::EndPair& ends) noexcept { MetaLog::reset(ends); });
}

}  // namespace

int main() {
    pair_of_one_split_resets_it();
    tokens_of_two_rings_end_the_process();
    ::fixy::check::pass(kTest, "test_reset_of_two_ends: PASSED\n");
    return 0;
}
