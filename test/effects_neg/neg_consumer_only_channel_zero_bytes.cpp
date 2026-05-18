// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 4 for fixy-A3-026 (signalling-channel shapes).
//
// Premise: ConsumerOnlyChannel<Bytes, Consumers> declares a sink-side
// signalling channel with N consumers and no in-band producer (audit
// log readers, Cipher event-log scrapers, replay-from-disk paths).
// Bytes = 0 is meaningless — ctx_workload::Unspecified is the
// sentinel for "no budget declared".  This fixture pins the Bytes
// rejection at the type level so call sites can't accidentally
// degenerate-spell the hint.
//
// Symmetric witness to ProducerOnlyChannel's zero-Bytes case — proves
// the static_assert on Bytes > 0 fires on the consumer-only shape too,
// independently of the producer-only shape.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "Bytes > 0" / "ConsumerOnlyChannel".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

namespace {

using ZeroByteChannel = eff::ctx_workload::ConsumerOnlyChannel<
    /*Bytes=*/0, /*Consumers=*/4>;

ZeroByteChannel witness{};

}  // namespace

int main() { (void)witness; return 0; }
