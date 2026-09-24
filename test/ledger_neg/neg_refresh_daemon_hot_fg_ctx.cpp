// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 3 of 3 for mint_refresh_daemon (#67).
//
// Violation: HotFgCtx carries effects::Row<> — it claims NOTHING.  It
// misses all three of the daemon's conjuncts at once: no IO to touch the
// kernel, no Block to wait on a disk, no Bg to run a thread.
//
// The other two fixtures each remove one conjunct from a context that
// otherwise fits.  This one is the blunt case, and it is worth keeping
// separate because it is the mistake with the worst consequence: a hot
// dispatch path that started a refresh thread would put probe threads,
// two hundred and fifty-six mebibytes of scratch mapping and a disk
// commit behind an operation whose whole design budget is a metadata
// write.
//
// Expected diagnostic: "no matching function for call to
// 'mint_refresh_daemon'" with a note that the constraint
// 'CtxFitsRefreshDaemon<...>' was not satisfied.

#include <crucible/ledger/RefreshDaemon.h>

namespace ledger = crucible::ledger;
namespace effects = crucible::effects;

static_assert(!ledger::CtxFitsRefreshDaemon<effects::HotFgCtx>, "premise: a foreground context claims nothing");

constexpr effects::HotFgCtx g_hot_ctx{};

// The line under test.  The hot path consults the ledger; it does not
// refresh it.
auto g_daemon = ledger::mint_refresh_daemon(g_hot_ctx, ledger::RefreshDaemonConfig{});
