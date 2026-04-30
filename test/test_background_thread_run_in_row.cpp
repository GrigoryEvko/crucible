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
#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include "test_assert.h"

#include <atomic>
#include <chrono>
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
// Audit-D — Cross-fence compositional consistency.  The bg drain
// loop (run_in_row) is a downstream caller of Cipher::record_event
// (FOUND-I09).  By transitivity of Subrow, any caller satisfying
// run_required_row MUST also satisfy record_event_required_row,
// otherwise the bg cannot legitimately invoke advance_head/log
// writes during region commit.  The test proves this row inclusion
// at compile time.
//
// Catches a future regression where the bg's row is silently
// narrowed (e.g. drops Block) but Cipher::record_event still
// requires Block — the inclusion below would fire and a CI signal
// arrives BEFORE any caller runs into the actual constraint
// violation downstream.

static void test_audit_d_cross_fence_consistency() {
    using BgRow     = BackgroundThread::run_required_row;
    using RecordRow = ::crucible::Cipher::record_event_required_row;

    // BgRow must contain RecordRow's atoms — the bg drain admits
    // every effect that record_event requires.
    static_assert(eff::Subrow<RecordRow, BgRow>,
        "BackgroundThread::run_required_row MUST contain "
        "Cipher::record_event_required_row.  The bg drain calls "
        "record_event during region commit — if the bg's row "
        "doesn't admit IO+Block, the call site cannot satisfy the "
        "downstream fence.");

    // Per-atom check — record_event needs IO + Block; the bg row
    // has BOTH.  Symmetric inclusion would mean BgRow == RecordRow,
    // which is FALSE: the bg additionally admits Bg + Alloc.
    static_assert(eff::row_contains_v<BgRow, eff::Effect::IO>);
    static_assert(eff::row_contains_v<BgRow, eff::Effect::Block>);
    static_assert(!eff::Subrow<BgRow, RecordRow>,
        "BgRow ⊋ RecordRow — the bg drain admits strictly more "
        "than record_event requires (Bg + Alloc are extra).");

    // Cardinality witness: |BgRow| > |RecordRow|.
    static_assert(eff::row_size_v<BgRow> > eff::row_size_v<RecordRow>);
    static_assert(eff::row_size_v<BgRow> == 4u);
    static_assert(eff::row_size_v<RecordRow> == 2u);

    std::printf("  audit-D cross_fence_consistency:           PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit-E — Saturation-minus-one matrix.  At cardinality 5 (every
// atom of the OS universe minus one), exactly four out of six
// "missing one atom" rows are valid Subrows of run_required_row's
// supersets — the four where the missing atom is Init or Test (not
// in run_required_row) become legitimate supersets, and the four
// where the missing atom IS in run_required_row (Bg/Alloc/IO/Block)
// must REJECT.
//
// Catches a regression where the fence is silently widened to "any
// row of cardinality ≥ 4" — a check based on size rather than atom
// membership.  At cardinality 5, the fence still fires for the
// missing-required-atom rows.

static void test_audit_e_saturation_minus_one_matrix() {
    using R = BackgroundThread::run_required_row;

    // Saturation-minus-Init: every atom EXCEPT Init.  Required
    // atoms all present → ACCEPT.
    using MinusInit = eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO,
        eff::Effect::Block, eff::Effect::Test>;
    static_assert(eff::Subrow<R, MinusInit>);
    static_assert(eff::row_size_v<MinusInit> == 5u);

    // Saturation-minus-Test: similar.
    using MinusTest = eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO,
        eff::Effect::Block, eff::Effect::Init>;
    static_assert(eff::Subrow<R, MinusTest>);

    // Saturation-minus-Bg: 5 atoms but Bg missing → REJECT.
    using MinusBg = eff::Row<
        eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
        eff::Effect::Init,  eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusBg>);
    static_assert(eff::row_size_v<MinusBg> == 5u);

    // Saturation-minus-Alloc: 5 atoms but Alloc missing → REJECT.
    using MinusAlloc = eff::Row<
        eff::Effect::Bg,    eff::Effect::IO, eff::Effect::Block,
        eff::Effect::Init,  eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusAlloc>);

    // Saturation-minus-IO: 5 atoms but IO missing → REJECT.
    using MinusIo = eff::Row<
        eff::Effect::Bg,    eff::Effect::Alloc, eff::Effect::Block,
        eff::Effect::Init,  eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusIo>);

    // Saturation-minus-Block: 5 atoms but Block missing → REJECT.
    using MinusBlock = eff::Row<
        eff::Effect::Bg,    eff::Effect::Alloc, eff::Effect::IO,
        eff::Effect::Init,  eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusBlock>);

    // Confirm: of the 6 "missing one atom" rows at cardinality 5,
    // exactly 2 accept (missing Init or Test, the orthogonal atoms)
    // and 4 reject (missing Bg/Alloc/IO/Block, the required atoms).
    // Cardinality-based "≥ 4" check would accept all 6.

    std::printf("  audit-E saturation_minus_one_matrix:       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit-F — Concurrent SPSC drain witness.  Mirrors FOUND-I17-AUDIT's
// concurrent SPSC test: spawns the bg via run_in_row<Required> with
// running=true, fg pushes a small batch into the TraceRing, signals
// stop, joins.  Validates:
//   • run_in_row honors the SPSC contract (head/tail acquire/release)
//   • the row-typed wrapper is a TRUE thin forwarder, not a re-
//     implementation that adds synchronization
//   • the trailing-drain path completes without hanging
//
// The test is structurally simple: we are NOT exercising the full
// IterationDetector machinery (no K=5 signature, no region build).
// We push ENTRIES that look like ops with no tensor metadata so the
// detector advances but never closes a region.  The point is to
// observe ring drain proceeds via the row-typed entry point.

static void test_audit_f_concurrent_spsc_drain() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.running.store(true, std::memory_order_release);

    using Required = BackgroundThread::run_required_row;

    // Spawn the bg via run_in_row<Required>.  The fence is
    // satisfied; the bg blocks on try_pop_batch + spin_pause.
    std::thread bg_thread([&]() {
        bt.run_in_row<Required>();
    });

    // Push a small batch.  Each entry has a unique schema_hash so
    // the IterationDetector won't fire (we just want drain motion).
    constexpr uint32_t N = 8;
    for (uint32_t i = 0; i < N; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0x1000ULL + i};
        e.shape_hash  = crucible::ShapeHash{0x2000ULL + i};
        // Push via the regular (non-row-typed) try_append_pinned API
        // — fg-side row-typing is a separate fence (FOUND-I16).
        // Here we test ONLY the consumer side's row fence.
        // try_append_pinned returns HotPath<Hot, bool>; .peek()
        // unwraps to const bool& for the loop predicate.
        while (!ring->try_append_pinned(
                  e, crucible::MetaIndex::none(),
                  crucible::ScopeHash{0}, crucible::CallsiteHash{0}).peek()) {
            std::this_thread::yield();
        }
    }

    // Allow bg to drain.  We bound the wait by polling
    // total_processed; once it reaches N, the bg has consumed
    // every entry through try_pop_batch.
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(5);
    while (bt.total_processed.load() < N
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(bt.total_processed.load() >= N);

    // Signal stop and join.  The bg's loop body sees running=false
    // and falls through to the trailing-drain path (which is empty).
    bt.running.store(false, std::memory_order_release);
    bg_thread.join();

    std::printf("  audit-F concurrent_spsc_drain:             PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit-G — F* alias closure under Subrow.  For every alias in
// FxAliases.h, either it fully satisfies the run_required_row fence
// or it fully rejects — there are no "partially satisfying" cases.
// Catches a regression where a future alias rename mutates one
// alias to a partial position (e.g. STRow gains Bg, becomes
// "almost-required-but-missing-Alloc").

static void test_audit_g_f_star_alias_closure() {
    using R = BackgroundThread::run_required_row;

    // Aliases the F* lattice declares — see FxAliases.h.
    static_assert(!eff::Subrow<R, eff::PureRow>);
    static_assert(!eff::Subrow<R, eff::TotRow>);
    static_assert(!eff::Subrow<R, eff::GhostRow>);
    static_assert(!eff::Subrow<R, eff::DivRow>);
    static_assert(!eff::Subrow<R, eff::STRow>);
    static_assert( eff::Subrow<R, eff::AllRow>);

    // Closure witness: 5 of 6 F* aliases reject; 1 accepts.  The
    // chain (Pure ⊑ Tot ⊑ Ghost ⊑ Div ⊑ ST ⊑ All) means rejection
    // propagates "downward" through the chain — once an alias
    // contains the required row, every superset alias does too.
    // The accepting boundary is between STRow and AllRow.
    static_assert(eff::is_subrow_v<eff::STRow, eff::AllRow>);
    static_assert(!eff::is_subrow_v<eff::AllRow, eff::STRow>);

    // The atom that flips the boundary is exactly Bg (since STRow
    // already has Block + Alloc + IO; the gap to AllRow is
    // {Bg, Init, Test}, but only Bg is in run_required_row).
    using StPlusBg = eff::Row<
        eff::Effect::Block, eff::Effect::Alloc, eff::Effect::IO,
        eff::Effect::Bg>;
    static_assert(eff::Subrow<R, StPlusBg>);
    // STRow alone (no Bg) must fail.
    static_assert(!eff::Subrow<R, eff::STRow>);

    std::printf("  audit-G f_star_alias_closure:              PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit-H — Forwarder-fidelity at signature level.  Prove
// run_in_row<R>() has the SAME signature shape as run():
//   • return type void
//   • zero runtime parameters
//   • non-noexcept (run() is non-noexcept; the forwarder must
//     preserve this — adding noexcept would change exception
//     semantics, even though we run with -fno-exceptions)
//   • non-const, non-volatile member (matches run())
//
// Catches a regression where a future refactor accidentally
// introduces a noexcept specifier, an extra parameter, or
// changes return type to bool/expected — any of which would
// break the "thin forwarder" contract.

static void test_audit_h_forwarder_fidelity_signature() {
    using Required = BackgroundThread::run_required_row;

    // Return type is void.  Probed via decltype on an
    // unevaluated call expression.
    BackgroundThread* p = nullptr;
    using RunInRowRet    = decltype(p->template run_in_row<Required>());
    static_assert(std::is_same_v<RunInRowRet, void>);

    // noexcept-ness witness.  run_in_row<R>() is non-noexcept (the
    // bg loop body invokes std::vector::push_back, which can throw
    // bad_alloc; under -fno-exceptions this terminates, but the
    // type-system signature still records non-noexcept).
    static_assert(!noexcept(p->template run_in_row<Required>()),
        "run_in_row<R>() must be non-noexcept — matching run()'s "
        "signature.  A thin forwarder cannot strengthen the spec.");

    // Pointer-to-member-function shape: void(BackgroundThread::*)()
    // — no parameters, void return, non-const, non-volatile,
    // non-noexcept.  The MFN type encodes every signature axis.
    static_assert(std::is_same_v<
        decltype(&BackgroundThread::template run_in_row<Required>),
        void (BackgroundThread::*)()>);

    // Universe-row instantiation has identical MFN shape.
    static_assert(std::is_same_v<
        decltype(&BackgroundThread::template run_in_row<eff::AllRow>),
        void (BackgroundThread::*)()>);

    // SuperRow (Bg + Alloc + IO + Block + Init + Test) instantiation
    // has identical MFN shape.  Confirms template parameter does
    // NOT leak into the function type.
    using SuperRow = eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO,
        eff::Effect::Block, eff::Effect::Init, eff::Effect::Test>;
    static_assert(std::is_same_v<
        decltype(&BackgroundThread::template run_in_row<SuperRow>),
        void (BackgroundThread::*)()>);

    // The MFN address is distinct per template instantiation
    // (different specialization → different code addresses), but
    // shape identity below confirms ABI compatibility — any caller
    // can dispatch to any instantiation through the same MFN slot.
    constexpr auto p1 = &BackgroundThread::template run_in_row<Required>;
    constexpr auto p2 = &BackgroundThread::template run_in_row<eff::AllRow>;
    static_assert(std::is_same_v<decltype(p1), decltype(p2)>);

    std::printf("  audit-H forwarder_fidelity_signature:      PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit-I — Large-batch drain through run_in_row.  Pushes a batch
// well over BATCH_SIZE-aligned thresholds (32 unique-hash entries)
// and verifies the row-typed entry drains them all without hang or
// regression.  The hashes are intentionally unique so the
// IterationDetector signature never fires — exercising
// on_iteration_boundary() would require sealing the global schema
// and ckernel tables (start() does this; this test bypasses start()
// to exercise run_in_row directly), so we keep the drain loop on
// the no-boundary code path.
//
// Catches a regression where run_in_row's drain loop terminates
// prematurely (e.g. exits the inner-loop before consuming the full
// drained_count batch).  Validates per-entry advance through the
// drain machinery.

static void test_audit_i_large_batch_drain() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.running.store(true, std::memory_order_release);

    using Required = BackgroundThread::run_required_row;

    std::thread bg_thread([&]() {
        bt.run_in_row<Required>();
    });

    // 32 unique schema_hashes — IterationDetector signature never
    // completes (each entry breaks any partial K-match), so
    // on_iteration_boundary() is never called.  Pure drain motion.
    constexpr uint32_t TOTAL = 32;
    for (uint32_t i = 0; i < TOTAL; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0xABCD'0001ULL + i};
        e.shape_hash  = crucible::ShapeHash{0xDEAD'0001ULL + i};
        while (!ring->try_append_pinned(
                  e, crucible::MetaIndex::none(),
                  crucible::ScopeHash{0},
                  crucible::CallsiteHash{0}).peek()) {
            std::this_thread::yield();
        }
    }

    // Wait for total_processed to reach TOTAL.  Bound by deadline.
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(5);
    while (bt.total_processed.load() < TOTAL
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const uint64_t processed_after_push = bt.total_processed.load();
    assert(processed_after_push >= TOTAL);

    // Stop and join.
    bt.running.store(false, std::memory_order_release);
    bg_thread.join();

    // Sanity: with unique hashes, IterationDetector's signature
    // never fully matched twice, so iterations_completed is 0.
    // The bg drained entries WITHOUT firing the boundary path.
    assert(bt.iterations_completed.get() == 0u);

    std::printf("  audit-I large_batch_drain:                 "
                "PASSED (processed=%llu of %u)\n",
                static_cast<unsigned long long>(processed_after_push),
                TOTAL);
}

// ─────────────────────────────────────────────────────────────────────
// Audit-J — Re-arm cycle.  spawn → stop → spawn → stop sequence
// via run_in_row.  Validates the row-typed entry tolerates re-entry
// and the running flag re-arms cleanly.  Each cycle pushes a small
// batch and waits for drain.
//
// Catches a regression where the wrapper accidentally captures
// state that prevents a second invocation — e.g. a thread_local
// once-flag, a static guard inside the template instantiation, or
// a destructor that was supposed to fire but didn't.

static void test_audit_j_rearm_cycle() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});

    using Required = BackgroundThread::run_required_row;
    constexpr uint32_t PER_CYCLE = 4;

    uint64_t prev_processed = 0;

    for (uint32_t cycle = 0; cycle < 2; ++cycle) {
        bt.running.store(true, std::memory_order_release);

        std::thread bg([&]() {
            bt.run_in_row<Required>();
        });

        for (uint32_t i = 0; i < PER_CYCLE; ++i) {
            crucible::TraceRing::Entry e{};
            // Distinct hashes per cycle so the IterationDetector
            // doesn't accidentally tie the two cycles together.
            e.schema_hash = crucible::SchemaHash{
                0x10000ULL + (cycle << 16) + i};
            e.shape_hash  = crucible::ShapeHash{0x20000ULL + i};
            while (!ring->try_append_pinned(
                      e, crucible::MetaIndex::none(),
                      crucible::ScopeHash{0},
                      crucible::CallsiteHash{0}).peek()) {
                std::this_thread::yield();
            }
        }

        const uint64_t target = prev_processed + PER_CYCLE;
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::seconds(5);
        while (bt.total_processed.load() < target
            && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(bt.total_processed.load() >= target);

        bt.running.store(false, std::memory_order_release);
        bg.join();

        prev_processed = bt.total_processed.load();
    }

    // After 2 cycles, total_processed >= 2 * PER_CYCLE — both
    // invocations of run_in_row observably consumed entries.
    assert(bt.total_processed.load() >= 2 * PER_CYCLE);

    std::printf("  audit-J rearm_cycle:                       "
                "PASSED (total=%llu over 2 cycles)\n",
                static_cast<unsigned long long>(bt.total_processed.load()));
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
    test_audit_d_cross_fence_consistency();
    test_audit_e_saturation_minus_one_matrix();
    test_audit_f_concurrent_spsc_drain();
    test_audit_g_f_star_alias_closure();
    test_audit_h_forwarder_fidelity_signature();
    test_audit_i_large_batch_drain();
    test_audit_j_rearm_cycle();

    std::printf("test_background_thread_run_in_row: 7 + 10 audit "
                "groups, all passed\n");
    return 0;
}
