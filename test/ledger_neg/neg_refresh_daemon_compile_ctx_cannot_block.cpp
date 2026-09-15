// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 3 for mint_refresh_daemon (#67).
//
// Violation: BgCompileCtx carries Row<Bg, Alloc, IO>.  It is a genuine
// background context — it claims effects::Bg — and it claims effects::IO
// because compiling writes kernel artifacts.  What it does not claim is
// effects::Block.
//
// That omission is the whole point of the fixture.  A refresh commits the
// ledger to disk and waits for the write, and a context that never
// promised to park cannot acquire that permission by being a background
// context.  Bg and Block are separate claims and the daemon needs both.
//
// It pins the opposite conjunct from neg_refresh_daemon_test_ctx_lacks_bg:
// that one has Block and no Bg, this one has Bg and no Block.  Between
// them the two halves of the added clause are each witnessed alone.
//
// Expected diagnostic: "no matching function for call to
// 'mint_refresh_daemon'" with a note that the constraint
// 'CtxFitsRefreshDaemon<...>' was not satisfied.

#include <crucible/ledger/RefreshDaemon.h>

namespace ledger = crucible::ledger;
namespace effects = crucible::effects;

static_assert(effects::CtxOwnsCapability<effects::BgCompileCtx, effects::Effect::Bg>,
              "premise: the compile context really is a background context");
static_assert(!ledger::CtxFitsRefreshDaemon<effects::BgCompileCtx>, "premise: it does not claim effects::Block");

constexpr effects::BgCompileCtx g_compile_ctx{};

// The line under test.  Being a background context does not by itself
// permit waiting on a disk.
auto g_daemon = ledger::mint_refresh_daemon(g_compile_ctx, ledger::RefreshDaemonConfig{});
