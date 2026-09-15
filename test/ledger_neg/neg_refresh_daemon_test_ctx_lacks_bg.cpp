// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 3 for mint_refresh_daemon (#67).
//
// Violation: TestRunnerCtx carries Row<Test, Alloc, IO, Block>.  It has
// both conjuncts CtxFitsLedgerStore asks for, so it can read and write a
// ledger file — test_ledger does exactly that.  What it does not carry is
// effects::Bg, and the daemon's clause adds that conjunct because the
// daemon spawns a thread and runs measurements on it.
//
// This is the fixture that matters most, because it is the one a
// regression would sail past.  Dropping Bg from CtxFitsRefreshDaemon
// leaves the concept identical to CtxFitsLedgerStore, and the other two
// fixtures — which fail on IO and on Block — would keep passing.  Only
// this one notices that the background claim stopped being required, and
// with it the rule that a fixture context cannot stand in for a
// background thread.
//
// Expected diagnostic: "no matching function for call to
// 'mint_refresh_daemon'" with a note that the constraint
// 'CtxFitsRefreshDaemon<...>' was not satisfied.

#include <crucible/ledger/RefreshDaemon.h>

namespace ledger = crucible::ledger;
namespace effects = crucible::effects;

// The premises, asserted so the fixture fails for the stated reason and
// not because TestRunnerCtx changed shape underneath it.  The first says
// this context really can work the store; the second says it really
// cannot claim the background effect.
static_assert(ledger::CtxFitsLedgerStore<effects::TestRunnerCtx>,
              "premise: a fixture context can read and write the ledger file");
static_assert(!ledger::CtxFitsRefreshDaemon<effects::TestRunnerCtx>,
              "premise: a fixture context does not claim effects::Bg");

constexpr effects::TestRunnerCtx g_test_ctx{};

// The line under test.  Reading a ledger is not the same permission as
// running a thread that refreshes one.
auto g_daemon = ledger::mint_refresh_daemon(g_test_ctx, ledger::RefreshDaemonConfig{});
