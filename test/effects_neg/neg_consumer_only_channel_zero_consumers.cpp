// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #4 of 4 for fixy-A3-026 (signalling-channel shapes).
//
// Premise: ConsumerOnlyChannel exists precisely so call sites can
// declare "N consumers, no producer" — Consumers = 0 is internally
// contradictory and IS just ctx_workload::Unspecified.  The
// static_assert pins this at the type level so the substrate never
// sees the degenerate shape.
//
// Together with the three companion fixtures
// (neg_producer_only_channel_zero_bytes,
//  neg_producer_only_channel_zero_producers,
//  neg_consumer_only_channel_zero_bytes) this pins ALL FOUR
// orthogonal degenerate shapes for the signalling-channel hints —
// proving every static_assert in the bodies of ProducerOnlyChannel /
// ConsumerOnlyChannel is load-bearing, no half-broken impl could
// silently admit any degenerate shape.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "Consumers > 0" /
//   "ConsumerOnlyChannel".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

namespace {

using ZeroConsumerChannel = eff::ctx_workload::ConsumerOnlyChannel<
    /*Bytes=*/1024, /*Consumers=*/0>;

ZeroConsumerChannel witness{};

}  // namespace

int main() { (void)witness; return 0; }
