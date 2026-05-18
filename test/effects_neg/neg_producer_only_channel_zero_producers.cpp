// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 4 for fixy-A3-026 (signalling-channel shapes).
//
// Premise: ProducerOnlyChannel exists precisely so call sites can
// declare "N producers, no consumer" — Producers = 0 is internally
// contradictory.  A ProducerOnlyChannel with no producers IS just
// ctx_workload::Unspecified.  The static_assert pins this at the
// type level so the substrate never sees the degenerate shape.
//
// This is the orthogonal-bit-class witness for the Bytes = 0 case:
// together with neg_producer_only_channel_zero_bytes.cpp we prove
// both static_asserts in the body are load-bearing — a half-broken
// impl that checks only one would COMPILE this fixture while the
// other one stays correct.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "Producers > 0" /
//   "ProducerOnlyChannel".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

namespace {

using ZeroProducerChannel = eff::ctx_workload::ProducerOnlyChannel<
    /*Bytes=*/1024, /*Producers=*/0, /*LatestOnly=*/true>;

ZeroProducerChannel witness{};

}  // namespace

int main() { (void)witness; return 0; }
