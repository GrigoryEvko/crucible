// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 4 for fixy-A3-026 (signalling-channel shapes).
//
// Premise: ProducerOnlyChannel<Bytes, Producers, LatestOnly> declares
// a producer-only fan shape (telemetry emitter, fire-and-forget
// broadcast).  Bytes = 0 is meaningless — a workload hint that claims
// "0 bytes of traffic" is either Unspecified (use the sentinel in
// ctx_workload) or a typo.  ChannelBudget already pins this at the
// type level via static_assert; ProducerOnlyChannel inherits the
// same discipline.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "Bytes > 0" / "ProducerOnlyChannel".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

namespace {

using ZeroByteChannel = eff::ctx_workload::ProducerOnlyChannel<
    /*Bytes=*/0, /*Producers=*/4, /*LatestOnly=*/true>;

ZeroByteChannel witness{};

}  // namespace

int main() { (void)witness; return 0; }
