// FOUND-I20 — BackgroundThread::run row-typed at compile time.
//
// The 8th-axiom fence on the bg-thread main loop.  Wraps `run()` with
// a compile-time effect-row constraint: callers must pass a Row that
// is a SUPERSET of {Bg, Alloc, IO, Block} — the four atoms the bg
// drain actually exhibits (Bg context tag + arena alloc during region
// build + IO via region_ready_cb + Block on SPSC pause).
//
// This is the structural inverse of FOUND-I16/I17/I19 (those use
// `IsPure<CallerRow>` — caller declares AT MOST nothing).  Here the
// direction is `Subrow<run_required_row, CallerRow>` — caller declares
// AT LEAST {Bg, Alloc, IO, Block}.  Same algebraic machinery, opposite
// polarity.  Mirrors Cipher::record_event's row fence (FOUND-I09).
//
// This file is the POSITIVE side of the fence.  The neg-compile
// witnesses (empty row, single-atom row, missing-atom row) live in
// test/safety_neg/neg_background_thread_run_in_row_*.cpp.
//
// Test surface (T01-T07 + 3 audit groups):
//   T01 — run_required_row IS exactly Row<Bg, Alloc, IO, Block>
//   T02 — Subrow concept witness — accepted shapes (saturation,
//         permutations, AllRow, supersets)
//   T03 — Subrow concept witness — rejected shapes (Pure, single-atom,
//         missing-atom-per-axis matrix)
//   T04 — API surface pinned: run_in_row<RequiredRow> is a member
//         template that takes no runtime args
//   T05 — Runtime smoke: spawn a controlled jthread that calls
//         run_in_row<RequiredRow>, signal stop immediately, join
//   T06 — F* alias compatibility: AllRow accepted (universe row)
//   T07 — Multi-row callers: Bg + ST union, Bg + AllRow union
//   audit-A — run_required_row content fence: header-level
//             static_asserts catch refactor-driven widening/narrowing
//   audit-B — Per-axis missing-atom rejection witnesses
//             (predicate-level — neg-compile fixtures verify the
//             template-substitution failure)
//   audit-C — F* alias predicate matrix: Pure/Tot/Ghost rejected,
//             Div/ST partially rejected, AllRow/STRow superset accepted

#include <crucible/BackgroundThread.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include "test_assert.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>

using crucible::BackgroundThread;
using crucible::TraceRing;
using crucible::MetaLog;
namespace eff = crucible::effects;

// ─────────────────────────────────────────────────────────────────────
// T01 — run_required_row IS exactly Row<Bg, Alloc, IO, Block>.

static void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<
        BackgroundThread::run_required_row,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO, eff::Effect::Block>>,
        "BackgroundThread::run_required_row MUST be exactly "
        "Row<Bg, Alloc, IO, Block>.  Adding atoms here is a "
        "deliberate API tightening that breaks every existing bg "
        "spawn site (Vigil setup, Keeper init, tests).");

    // Per-atom membership witnesses.
    static_assert( eff::row_contains_v<
        BackgroundThread::run_required_row, eff::Effect::Bg>);
    static_assert( eff::row_contains_v<
        BackgroundThread::run_required_row, eff::Effect::Alloc>);
    static_assert( eff::row_contains_v<
        BackgroundThread::run_required_row, eff::Effect::IO>);
    static_assert( eff::row_contains_v<
        BackgroundThread::run_required_row, eff::Effect::Block>);

    // Atoms NOT in the required row → Init/Test are context tags
    // for other entry points.  The bg loop does NOT exhibit them.
    static_assert(!eff::row_contains_v<
        BackgroundThread::run_required_row, eff::Effect::Init>);
    static_assert(!eff::row_contains_v<
        BackgroundThread::run_required_row, eff::Effect::Test>);

    static_assert(eff::row_size_v<
        BackgroundThread::run_required_row> == 4);

    std::printf("  T01 required_row_pinned:                   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T02 — Subrow concept witness: required ⊆ caller, accepted shapes.

static void test_t02_subrow_accepted_shapes() {
    using Required = BackgroundThread::run_required_row;

    // Caller row exactly matches required → accept.
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO, eff::Effect::Block>>);

    // Caller row in different Effect order → still accept (Subrow is
    // extensional, set-membership-based).
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Block, eff::Effect::IO,
                 eff::Effect::Alloc, eff::Effect::Bg>>);
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Alloc, eff::Effect::Bg,
                 eff::Effect::Block, eff::Effect::IO>>);

    // Plus-Init superset (Init context tag added) → accept.
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO, eff::Effect::Block,
                 eff::Effect::Init>>);

    // Plus-Test superset (Test context tag added) → accept.
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO, eff::Effect::Block,
                 eff::Effect::Test>>);

    // Universe superset (every Effect atom) → accept.
    static_assert(eff::Subrow<Required, eff::AllRow>);

    std::printf("  T02 subrow_accepted_shapes:                PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T03 — Subrow concept witness: required ⊄ caller, rejected shapes.
// These are compile-time predicate witnesses; the actual call-site
// rejection lives in neg-compile fixtures that exercise the
// template-substitution-failure diagnostic.

static void test_t03_subrow_rejected_shapes() {
    using Required = BackgroundThread::run_required_row;

    // Empty row (Hot/Pure context) → reject.
    static_assert(!eff::Subrow<Required, eff::Row<>>);
    static_assert(!eff::Subrow<Required, eff::PureRow>);
    static_assert(!eff::Subrow<Required, eff::TotRow>);
    static_assert(!eff::Subrow<Required, eff::GhostRow>);

    // Single-atom rows — each missing 3 of the required 4 atoms.
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Bg>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Alloc>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::IO>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Block>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Init>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Test>>);

    // Per-axis missing-atom matrix — each missing exactly one
    // required atom.  The neg-compile fixtures exercise these as
    // template-substitution failures.
    static_assert(!eff::Subrow<Required,
        eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                 eff::Effect::Block>>);  // missing Bg
    static_assert(!eff::Subrow<Required,
        eff::Row<eff::Effect::Bg, eff::Effect::IO,
                 eff::Effect::Block>>);  // missing Alloc
    static_assert(!eff::Subrow<Required,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::Block>>);  // missing IO
    static_assert(!eff::Subrow<Required,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO>>);     // missing Block

    // F* alias rejections: DivRow = {Block} only, STRow = {Block,
    // Alloc, IO} (missing Bg).
    static_assert(!eff::Subrow<Required, eff::DivRow>);
    static_assert(!eff::Subrow<Required, eff::STRow>);

    std::printf("  T03 subrow_rejected_shapes:                PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T04 — API surface pinned.  run_in_row<RequiredRow> is a member
// template returning void.  Unevaluated decltype proves the type
// without invoking the call.

static void test_t04_api_surface_pinned() {
    using Required = BackgroundThread::run_required_row;

    // The wrapper's declared return type is void (matches run()).
    // SFINAE-friendly probe — if the constraint is broken or the
    // signature changes, this static_assert catches it.
    BackgroundThread* bt_ptr = nullptr;
    using ReturnT = decltype(bt_ptr->template run_in_row<Required>());
    static_assert(std::is_same_v<ReturnT, void>,
        "run_in_row<RequiredRow> must return void (matches run()).");

    // Universe row also satisfies the constraint.
    using ReturnT2 = decltype(bt_ptr->template run_in_row<eff::AllRow>());
    static_assert(std::is_same_v<ReturnT2, void>);

    std::printf("  T04 api_surface_pinned:                    PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T05 — Runtime smoke.  Set up the bg thread state, signal stop
// FIRST, then call run_in_row<RequiredRow> from a controlled thread.
// run() drains the trailing batch (which is empty) and returns.  No
// allocation, no assertion fires.  Validates the wrapper actually
// forwards to run() correctly.

static void test_t05_runtime_smoke() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();

    // Wire the WriteOnce<NonNull> handles.  No actual draining — we
    // pre-clear running so the bg loop body is skipped entirely.
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.running.store(false, std::memory_order_relaxed);

    using Required = BackgroundThread::run_required_row;

    // Spawn a controlled thread that calls run_in_row directly.
    // Since running=false, run()'s while-loop body never executes;
    // it falls through to the trailing-drain (which is empty).
    bool done = false;
    std::thread t([&]() {
        bt.run_in_row<Required>();
        done = true;
    });
    t.join();
    assert(done);

    std::printf("  T05 runtime_smoke:                         PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T06 — F* alias compatibility.  AllRow (universe row) is the
// canonical "anything goes" row; it must accept run_in_row.

static void test_t06_f_star_alias_all_row() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.running.store(false, std::memory_order_relaxed);

    bool done = false;
    std::thread t([&]() {
        bt.run_in_row<eff::AllRow>();
        done = true;
    });
    t.join();
    assert(done);

    std::printf("  T06 f_star_alias_all_row:                  PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T07 — Multi-row callers via union: caller declares Required +
// extras (Init, Test).  The Subrow constraint checks set-membership,
// so adding extras can never break acceptance.

static void test_t07_multi_row_caller() {
    using Required = BackgroundThread::run_required_row;

    // Required + Init + Test → accept.  Note: SuperRow ≠ AllRow as
    // structural types (atom-order differs from AllRow's canonical
    // <Alloc, IO, Block, Bg, Init, Test> ordering), but BOTH carry
    // the same atom *set* — Subrow is set-membership-based, so both
    // satisfy the fence.
    using SuperRow = eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO,
        eff::Effect::Block, eff::Effect::Init, eff::Effect::Test>;
    static_assert(eff::Subrow<Required, SuperRow>);
    // Membership equality witness — both rows hold the same 6 atoms.
    static_assert(eff::Subrow<SuperRow, eff::AllRow>);
    static_assert(eff::Subrow<eff::AllRow, SuperRow>);

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.running.store(false, std::memory_order_relaxed);

    bool done = false;
    std::thread t([&]() {
        bt.run_in_row<SuperRow>();
        done = true;
    });
    t.join();
    assert(done);

    std::printf("  T07 multi_row_caller:                      PASSED\n");
}

// ─── FOUND-I20-AUDIT ────────────────────────────────────────────────
// Audit-A — Header-level content fence.  The static_asserts inside
// BackgroundThread.h catch refactor-driven widening (adding an atom)
// or narrowing (dropping an atom).  Re-pin the EXACT 4-atom contract
// from the test side as a second-source witness.

static void test_audit_a_required_row_header_fence() {
    using R = BackgroundThread::run_required_row;

    // Exact-equality witness: any swap in atom set or count fails.
    static_assert(std::is_same_v<R,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO, eff::Effect::Block>>);
    static_assert(eff::row_size_v<R> == 4u);

    // Catches: swap Block → Init.  Below would silently widen the
    // fence to accept fg-thread-only callers.  The static_assert
    // above (and this one) fires first.
    static_assert(eff::row_contains_v<R, eff::Effect::Block>);
    static_assert(!eff::row_contains_v<R, eff::Effect::Init>);

    // Catches: drop Alloc.  Below would silently narrow the fence
    // and accept callers that don't admit allocation.
    static_assert(eff::row_contains_v<R, eff::Effect::Alloc>);

    std::printf("  audit-A required_row_header_fence:         PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit-B — Per-axis missing-atom rejection matrix.  Predicate-level
// witnesses for the four "missing exactly one atom" rows, mirroring
// the neg-compile fixtures one-for-one.  The neg-compile fixtures
// catch the template-substitution failure diagnostic; this audit
// asserts the underlying predicate at compile time.

static void test_audit_b_per_axis_missing_atom_matrix() {
    using R = BackgroundThread::run_required_row;

    // Missing-Bg: Row<Alloc, IO, Block> → reject.
    static_assert(!eff::Subrow<R,
        eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                 eff::Effect::Block>>);

    // Missing-Alloc: Row<Bg, IO, Block> → reject.
    static_assert(!eff::Subrow<R,
        eff::Row<eff::Effect::Bg, eff::Effect::IO,
                 eff::Effect::Block>>);

    // Missing-IO: Row<Bg, Alloc, Block> → reject.
    static_assert(!eff::Subrow<R,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::Block>>);

    // Missing-Block: Row<Bg, Alloc, IO> → reject.
    static_assert(!eff::Subrow<R,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO>>);

    // Adding back the missing atom flips the predicate to accept.
    static_assert(eff::Subrow<R,
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                 eff::Effect::IO, eff::Effect::Block>>);

    std::printf("  audit-B per_axis_missing_atom_matrix:      PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit-C — F* alias predicate matrix.  Document the F* alias chain
// against the fence: which aliases are accepted, which are rejected,
// and WHY.  Catches a regression where a future alias rename
// (e.g. STRow gaining Bg) silently changes acceptance.

static void test_audit_c_f_star_alias_matrix() {
    using R = BackgroundThread::run_required_row;

    // PureRow / TotRow / GhostRow = Row<>.  Empty row → reject all.
    static_assert(!eff::Subrow<R, eff::PureRow>);
    static_assert(!eff::Subrow<R, eff::TotRow>);
    static_assert(!eff::Subrow<R, eff::GhostRow>);

    // DivRow = Row<Block>.  Single atom → reject.
    static_assert(!eff::Subrow<R, eff::DivRow>);

    // STRow = Row<Block, Alloc, IO>.  Three of four required atoms
    // — missing Bg context tag → reject.
    static_assert(!eff::Subrow<R, eff::STRow>);

    // AllRow = universe → accept (only F* alias in the set that
    // satisfies the fence).
    static_assert(eff::Subrow<R, eff::AllRow>);

    // Sanity: the gap between STRow and AllRow is exactly Bg + Init
    // + Test.  Adding just Bg to STRow → accept.
    using StPlusBg = eff::Row<
        eff::Effect::Block, eff::Effect::Alloc, eff::Effect::IO,
        eff::Effect::Bg>;
    static_assert(eff::Subrow<R, StPlusBg>);

    std::printf("  audit-C f_star_alias_matrix:               PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
int main() {
    std::printf("test_background_thread_run_in_row — FOUND-I20 "
                "8th-axiom fence\n");
    test_t01_required_row_pinned();
    test_t02_subrow_accepted_shapes();
    test_t03_subrow_rejected_shapes();
    test_t04_api_surface_pinned();
    test_t05_runtime_smoke();
    test_t06_f_star_alias_all_row();
    test_t07_multi_row_caller();

    std::printf("--- FOUND-I20-AUDIT ---\n");
    test_audit_a_required_row_header_fence();
    test_audit_b_per_axis_missing_atom_matrix();
    test_audit_c_f_star_alias_matrix();

    std::printf("test_background_thread_run_in_row: 7 + 3 audit "
                "groups, all passed\n");
    return 0;
}
