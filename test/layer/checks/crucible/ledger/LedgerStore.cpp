// The compile-time checks of crucible/ledger/LedgerStore.h.

#include <crucible/ledger/LedgerStore.h>

namespace crucible::ledger {

static_assert(sizeof(LedgerIoCtx) == 1, "an execution context must stay a tag");

static_assert(CtxFitsLedgerStore<LedgerIoCtx>);
static_assert(CtxFitsLedgerStore<::fixy::TestRunnerCtx>);
// A foreground context claims nothing, so it cannot open a file.
static_assert(!CtxFitsLedgerStore<::fixy::HotFgCtx>);
// An initialization context claims IO but not Block, so it cannot wait on
// one either.
static_assert(!CtxFitsLedgerStore<::fixy::ColdInitCtx>);

namespace store_detail::self_test {

static_assert(kMaxLedgerEntries >= kVerdictIdCount);
static_assert(confidence_from_name("high") == Confidence::High);
static_assert(confidence_from_name("low") == Confidence::Low);
static_assert(confidence_from_name("unknown") == Confidence::Unknown);
static_assert(confidence_from_name("HIGH") == Confidence::Unknown, "the parse is exact, not case-folding");
static_assert(confidence_from_name("") == Confidence::Unknown);

// Confidence must order Unknown < Low < High for the read-back clamp in
// deserialize_ledger to mean what it says.
static_assert(Confidence::Unknown < Confidence::Low);
static_assert(Confidence::Low < Confidence::High);

}  // namespace store_detail::self_test

}  // namespace crucible::ledger
