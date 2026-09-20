// Corpus entry staleness_secret_without_declassify.  Hunt-Sands 2008
// 'Just Forget It' (POPL) formalizes information-erasure semantics and
// shows that a classified value reachable through a stale-replay window,
// with no freshness-discharging policy, is semantically a FAILED
// erasure: the data the policy would require be forgotten stays
// observable.
//
// The pack names the replay window on the strict Security pole, so the
// carrier is classified by default.  What makes this entry the one that
// demonstrates axis-matched discharge: the same pack with
// declassify<AuditedLogging> or declassify<WireSerialize> is STILL
// refused, because those policies authorize an export channel and say
// nothing about temporal replay.  Only
// declassify<secret_policy::AuthorizedReplay> discharges Staleness, and
// it is the one policy whose axes_discharged_of mask carries it.
// test/fixy/test_corpus.cpp holds all four of those cases.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::stale_to<100>> refused{};
    return 0;
}
