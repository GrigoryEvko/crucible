// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 2 for mint_ledger_view (#66).
//
// Violation: ColdInitCtx carries Row<Init, Alloc, IO>.  It CAN touch
// the kernel — so this is a different mismatch class from the
// foreground fixture, which claims nothing at all — but it cannot
// block.  cap_permitted_row<Init> in effects/ExecCtx.h deliberately
// omits Effect::Block: initialization is not permitted to park.
//
// Reading the ledger parks on a disk.  So the read genuinely does not
// belong on an initialization context, and the type system saying so is
// the correct answer rather than an inconvenience: a startup path that
// quietly acquired a disk stall is exactly the kind of unbudgeted
// latency this codebase exists to keep out.  The runtime's read
// therefore runs on a background context, which is what
// ledger::LedgerIoCtx names.
//
// The two fixtures together pin both halves of CtxFitsLedgerStore: one
// context missing IO and Block, one missing only Block.  A regression
// that dropped either conjunct would still be caught by the other.
//
// Expected diagnostic: "no matching function for call to
// 'mint_ledger_view'" with a note that the constraint
// 'CtxFitsLedgerStore<...>' was not satisfied.

#include <crucible/ledger/Ledger.h>

namespace ledger = crucible::ledger;
namespace effects = crucible::effects;

// The premises, asserted so the fixture fails for the stated reason.
// Unlike the foreground context, this one DOES admit IO — only Block is
// missing, which is what makes it a distinct mismatch class.
static_assert(effects::row_contains_v<effects::row_type_of_t<effects::ColdInitCtx>, effects::Effect::IO>,
              "premise: an initialization context may touch the kernel");
static_assert(!effects::row_contains_v<effects::row_type_of_t<effects::ColdInitCtx>, effects::Effect::Block>,
              "premise: an initialization context may not park");
static_assert(!ledger::CtxFitsLedgerStore<effects::ColdInitCtx>, "premise: the store needs both IO and Block");

// The background context, which differs only by admitting Block, is
// accepted. Without this the fixture could pass because the concept
// rejects everything.
static_assert(ledger::CtxFitsLedgerStore<ledger::LedgerIoCtx>,
              "control: a background context admits both and is accepted");

constexpr effects::ColdInitCtx g_init_ctx{};

// The line under test.  Claiming IO is not enough; the read blocks.
ledger::LedgerView g_view = ledger::mint_ledger_view(g_init_ctx, ledger::HostFingerprint{});
