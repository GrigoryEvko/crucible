// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 2 for mint_ledger_view (#66).
//
// Violation: HotFgCtx carries effects::Row<> — it claims NOTHING.  A
// foreground dispatch context holds no capability at all, by design:
// that is what makes the hot path auditable.  Opening and reading the
// ledger file needs effects::IO to touch the kernel and effects::Block
// to wait on a disk, so CtxFitsLedgerStore<HotFgCtx> is unsatisfied and
// the mint has no viable candidate.
//
// This is the mismatch class that matters most in practice.  The whole
// point of the ledger is that the hot path consults a value someone
// else measured; a hot-path caller that tried to load the file itself
// would turn a ~2 ns dispatch into a disk read.  The type system
// refuses it rather than leaving it to review.
//
// Expected diagnostic: "no matching function for call to
// 'mint_ledger_view'" with a note that the constraint
// 'CtxFitsLedgerStore<...>' was not satisfied.

#include <crucible/ledger/Ledger.h>

namespace ledger = crucible::ledger;
namespace effects = crucible::effects;

// The premise, asserted so the fixture fails for the stated reason and
// not because HotFgCtx changed shape underneath it.
static_assert(!ledger::CtxFitsLedgerStore<effects::HotFgCtx>, "premise: a foreground context claims no capability");

constexpr effects::HotFgCtx g_hot_ctx{};

// The line under test.  A hot-path context cannot mint a ledger view.
ledger::LedgerView g_view = ledger::mint_ledger_view(g_hot_ctx, ledger::HostFingerprint{});
