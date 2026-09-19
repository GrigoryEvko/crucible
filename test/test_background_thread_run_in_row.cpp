// The background drain loop exhibits four effect atoms: the background
// context tag itself, arena allocation while building a region, output
// through the region-ready callback, and blocking on the pause between
// queue reads. A caller must therefore declare at least those four.
//
// That direction is the inverse of the purity fences elsewhere, where a
// caller declares at most nothing. The algebra is the same and only the
// polarity differs. This file holds the accepting side; the rejecting side
// needs a compile to fail and lives with the negative fixtures.

#include <crucible/BackgroundThread.h>
#include <crucible/Cipher.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include "test_assert.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <span>
#include <thread>
#include <type_traits>

using crucible::BackgroundThread;
using crucible::TraceRing;
using crucible::MetaLog;
namespace eff = crucible::effects;

static void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<BackgroundThread::run_required_row,
                                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>,
                  "BackgroundThread::run_required_row is exactly "
                  "Row<Bg, Alloc, IO, Block>. Adding an atom tightens the API and "
                  "breaks every existing background spawn site.");

    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Bg>);
    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Alloc>);
    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::IO>);
    static_assert(eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Block>);

    // Init and Test tag other entry points. The drain loop exhibits neither.
    static_assert(!eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Init>);
    static_assert(!eff::row_contains_v<BackgroundThread::run_required_row, eff::Effect::Test>);

    static_assert(eff::row_size_v<BackgroundThread::run_required_row> == 4);

    std::printf("  T01 required_row_pinned:                   PASSED\n");
}

static void test_t02_subrow_accepted_shapes() {
    using Required = BackgroundThread::run_required_row;

    static_assert(
        eff::Subrow<Required, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>);

    // The relation is set membership, so the order the caller writes its
    // atoms in makes no difference.
    static_assert(
        eff::Subrow<Required, eff::Row<eff::Effect::Block, eff::Effect::IO, eff::Effect::Alloc, eff::Effect::Bg>>);
    static_assert(
        eff::Subrow<Required, eff::Row<eff::Effect::Alloc, eff::Effect::Bg, eff::Effect::Block, eff::Effect::IO>>);

    static_assert(eff::Subrow<Required, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO,
                                                 eff::Effect::Block, eff::Effect::Init>>);

    static_assert(eff::Subrow<Required, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO,
                                                 eff::Effect::Block, eff::Effect::Test>>);

    static_assert(eff::Subrow<Required, eff::AllRow>);

    std::printf("  T02 subrow_accepted_shapes:                PASSED\n");
}

// These assertions check the predicate. Rejecting an actual call needs a
// substitution failure, which only a fixture that fails to compile can show.
static void test_t03_subrow_rejected_shapes() {
    using Required = BackgroundThread::run_required_row;

    static_assert(!eff::Subrow<Required, eff::Row<>>);
    static_assert(!eff::Subrow<Required, eff::PureRow>);
    static_assert(!eff::Subrow<Required, eff::TotRow>);
    static_assert(!eff::Subrow<Required, eff::GhostRow>);

    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Bg>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Alloc>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::IO>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Block>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Init>>);
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Test>>);

    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                                                  eff::Effect::Block>>);  // missing Bg
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Bg, eff::Effect::IO,
                                                  eff::Effect::Block>>);  // missing Alloc
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                                                  eff::Effect::Block>>);  // missing IO
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                                                  eff::Effect::IO>>);  // missing Block

    // The divergence row holds Block alone, and the state row holds Block,
    // Alloc and IO but no Bg.
    static_assert(!eff::Subrow<Required, eff::DivRow>);
    static_assert(!eff::Subrow<Required, eff::STRow>);

    std::printf("  T03 subrow_rejected_shapes:                PASSED\n");
}

// The pointer is never dereferenced. The call expression sits inside
// decltype, where it is unevaluated, and only its type is taken.
static void test_t04_api_surface_pinned() {
    using Required = BackgroundThread::run_required_row;

    BackgroundThread* bt_ptr = nullptr;
    using ReturnT = decltype(bt_ptr->template run_in_row<Required>());
    static_assert(std::is_same_v<ReturnT, void>, "run_in_row<RequiredRow> returns void, matching run().");

    using ReturnT2 = decltype(bt_ptr->template run_in_row<eff::AllRow>());
    static_assert(std::is_same_v<ReturnT2, void>);

    std::printf("  T04 api_surface_pinned:                    PASSED\n");
}

// Stop is signalled before the loop starts, so the loop body never runs and
// the call falls straight through to the trailing drain, which is empty. What
// is under test is that the wrapper forwards at all.
static void test_t05_runtime_smoke() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();

    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.stop_requested.signal();

    using Required = BackgroundThread::run_required_row;

    bool done = false;
    std::jthread t([&]() {
        bt.run_in_row<Required>();
        done = true;
    });
    t.join();
    assert(done);

    std::printf("  T05 runtime_smoke:                         PASSED\n");
}

// The universe row is the canonical way a caller says it admits anything, so
// it has to pass the fence.
static void test_t06_f_star_alias_all_row() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.stop_requested.signal();

    bool done = false;
    std::jthread t([&]() {
        bt.run_in_row<eff::AllRow>();
        done = true;
    });
    t.join();
    assert(done);

    std::printf("  T06 f_star_alias_all_row:                  PASSED\n");
}

// Declaring extra atoms can never cost a caller its acceptance.
static void test_t07_multi_row_caller() {
    using Required = BackgroundThread::run_required_row;

    // This row is not the same type as the universe row, which spells its
    // atoms in a different order, but the two hold the same set. Membership
    // is what the fence looks at, so both pass it.
    using SuperRow = eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
                              eff::Effect::Init, eff::Effect::Test>;
    static_assert(eff::Subrow<Required, SuperRow>);
    static_assert(eff::Subrow<SuperRow, eff::AllRow>);
    static_assert(eff::Subrow<eff::AllRow, SuperRow>);

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.stop_requested.signal();

    bool done = false;
    std::jthread t([&]() {
        bt.run_in_row<SuperRow>();
        done = true;
    });
    t.join();
    assert(done);

    std::printf("  T07 multi_row_caller:                      PASSED\n");
}

// The header asserts its own four-atom contract. This is a second source for
// the same claim, so a refactor that widens or narrows the row is caught from
// outside the header as well.
static void test_audit_a_required_row_header_fence() {
    using R = BackgroundThread::run_required_row;

    static_assert(
        std::is_same_v<R, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>);
    static_assert(eff::row_size_v<R> == 4u);

    // Swapping Block for Init would widen the fence to admit callers that
    // only ever run on the foreground thread.
    static_assert(eff::row_contains_v<R, eff::Effect::Block>);
    static_assert(!eff::row_contains_v<R, eff::Effect::Init>);

    // Dropping Alloc would admit callers that do not allow allocation.
    static_assert(eff::row_contains_v<R, eff::Effect::Alloc>);

    std::printf("  audit-A required_row_header_fence:         PASSED\n");
}

// One row per required atom, each dropping exactly that atom. These mirror
// the negative fixtures one for one at the predicate level.
static void test_audit_b_per_axis_missing_atom_matrix() {
    using R = BackgroundThread::run_required_row;

    // missing Bg
    static_assert(!eff::Subrow<R, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>);

    // missing Alloc
    static_assert(!eff::Subrow<R, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block>>);

    // missing IO
    static_assert(!eff::Subrow<R, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::Block>>);

    // missing Block
    static_assert(!eff::Subrow<R, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>>);

    // Restoring the fourth atom flips the predicate.
    static_assert(eff::Subrow<R, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>);

    std::printf("  audit-B per_axis_missing_atom_matrix:      PASSED\n");
}

// Which named rows clear the fence and which do not, with the content of each
// spelled out. An alias whose content changes would silently change its
// acceptance otherwise.
static void test_audit_c_f_star_alias_matrix() {
    using R = BackgroundThread::run_required_row;

    // The pure, total and ghost rows are all empty.
    static_assert(!eff::Subrow<R, eff::PureRow>);
    static_assert(!eff::Subrow<R, eff::TotRow>);
    static_assert(!eff::Subrow<R, eff::GhostRow>);

    // The divergence row holds Block alone.
    static_assert(!eff::Subrow<R, eff::DivRow>);

    // The state row holds three of the four required atoms and lacks Bg.
    static_assert(!eff::Subrow<R, eff::STRow>);

    // The universe row is the only named one that clears the fence.
    static_assert(eff::Subrow<R, eff::AllRow>);

    // The gap between the state row and the universe is Bg, Init and Test,
    // of which only Bg is required.
    using StPlusBg = eff::Row<eff::Effect::Block, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Bg>;
    static_assert(eff::Subrow<R, StPlusBg>);

    std::printf("  audit-C f_star_alias_matrix:               PASSED\n");
}

// The drain loop calls record_event while committing a region, so by
// transitivity its row has to contain record_event's row. Narrowing the drain
// row would otherwise leave that downstream call unsatisfiable, and this
// inclusion fires before any caller reaches the violation itself.
static void test_audit_d_cross_fence_consistency() {
    using BgRow = BackgroundThread::run_required_row;
    using RecordRow = ::crucible::Cipher::record_event_required_row;

    static_assert(eff::Subrow<RecordRow, BgRow>, "BackgroundThread::run_required_row contains "
                                                 "Cipher::record_event_required_row. The background drain calls "
                                                 "record_event while committing a region, so a drain row that does "
                                                 "not admit IO and Block leaves that call site unsatisfiable.");

    // Containment runs one way only. The drain row additionally admits Bg
    // and Alloc, so the two rows are not equal.
    static_assert(eff::row_contains_v<BgRow, eff::Effect::IO>);
    static_assert(eff::row_contains_v<BgRow, eff::Effect::Block>);
    static_assert(!eff::Subrow<BgRow, RecordRow>, "The background drain row strictly contains the record_event row: "
                                                  "Bg and Alloc are extra.");

    static_assert(eff::row_size_v<BgRow> > eff::row_size_v<RecordRow>);
    static_assert(eff::row_size_v<BgRow> == 4u);
    static_assert(eff::row_size_v<RecordRow> == 2u);

    std::printf("  audit-D cross_fence_consistency:           PASSED\n");
}

// Six rows of five atoms, each dropping one atom of the universe. Two of them
// drop Init or Test and pass; the other four drop a required atom and fail.
// A fence that tested the number of atoms rather than which ones would accept
// all six.
static void test_audit_e_saturation_minus_one_matrix() {
    using R = BackgroundThread::run_required_row;

    using MinusInit =
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Test>;
    static_assert(eff::Subrow<R, MinusInit>);
    static_assert(eff::row_size_v<MinusInit> == 5u);

    using MinusTest =
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Init>;
    static_assert(eff::Subrow<R, MinusTest>);

    // missing Bg
    using MinusBg =
        eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Init, eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusBg>);
    static_assert(eff::row_size_v<MinusBg> == 5u);

    // missing Alloc
    using MinusAlloc =
        eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block, eff::Effect::Init, eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusAlloc>);

    // missing IO
    using MinusIo =
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::Block, eff::Effect::Init, eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusIo>);

    // missing Block
    using MinusBlock =
        eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Init, eff::Effect::Test>;
    static_assert(!eff::Subrow<R, MinusBlock>);

    std::printf("  audit-E saturation_minus_one_matrix:       PASSED\n");
}

// A real drain, with one thread pushing while the row-typed entry point
// consumes. What it establishes is that the wrapper forwards rather than
// reimplements, so the queue's acquire and release discipline is untouched
// and the trailing drain does not hang.
//
// Nothing here builds a region. The entries carry no tensor metadata, so the
// iteration detector advances but never closes anything, which leaves plain
// drain motion to observe.
static void test_audit_f_concurrent_spsc_drain() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.stop_requested.reset_in_quiescent_context(crucible::safety::OneShotFlag::QuiescenceProof{});

    using Required = BackgroundThread::run_required_row;

    std::jthread bg_thread([&]() { bt.run_in_row<Required>(); });

    // Each entry carries its own schema hash so the iteration detector never
    // matches a signature.
    constexpr uint32_t N = 8;
    for (uint32_t i = 0; i < N; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0x1000ULL + i};
        e.shape_hash = crucible::ShapeHash{0x2000ULL + i};
        // The push side has a fence of its own, which is not what is under
        // test here, so this goes through the untyped append. That append
        // returns a wrapped bool, which peek unwraps for the predicate.
        while (
            !ring->try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})
                 .peek()) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // The processed counter reaching N is what says the drain consumed every
    // entry. The deadline keeps a stalled drain from hanging the suite.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bt.total_processed.load() < N && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.total_processed.load() >= N);

    bt.stop_requested.signal();
    bg_thread.join();

    std::printf("  audit-F concurrent_spsc_drain:             PASSED\n");
}

// Each named row either clears the fence or fails it outright; none sits in
// between. An alias that gained one required atom but not the rest would land
// in that in-between position and this group would catch it.
static void test_audit_g_f_star_alias_closure() {
    using R = BackgroundThread::run_required_row;

    static_assert(!eff::Subrow<R, eff::PureRow>);
    static_assert(!eff::Subrow<R, eff::TotRow>);
    static_assert(!eff::Subrow<R, eff::GhostRow>);
    static_assert(!eff::Subrow<R, eff::DivRow>);
    static_assert(!eff::Subrow<R, eff::STRow>);
    static_assert(eff::Subrow<R, eff::AllRow>);

    // The aliases form a chain, so once one of them contains the required
    // row every alias above it does too. The boundary sits between the state
    // row and the universe.
    static_assert(eff::is_subrow_v<eff::STRow, eff::AllRow>);
    static_assert(!eff::is_subrow_v<eff::AllRow, eff::STRow>);

    // Bg alone is what moves the boundary: the state row already holds the
    // other three required atoms.
    using StPlusBg = eff::Row<eff::Effect::Block, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Bg>;
    static_assert(eff::Subrow<R, StPlusBg>);
    static_assert(!eff::Subrow<R, eff::STRow>);

    std::printf("  audit-G f_star_alias_closure:              PASSED\n");
}

// The wrapper is only a forwarder if its signature matches the function it
// forwards to on every axis: void return, no parameters, noexcept, and a
// non-const non-volatile member. An extra parameter or a return type of bool
// would break that contract.
static void test_audit_h_forwarder_fidelity_signature() {
    using Required = BackgroundThread::run_required_row;

    BackgroundThread* p = nullptr;
    using RunInRowRet = decltype(p->template run_in_row<Required>());
    static_assert(std::is_same_v<RunInRowRet, void>);

    // The entry point is noexcept because an allocation or thread failure
    // terminates under the project's no-exception discipline rather than
    // crossing this boundary.
    static_assert(noexcept(p->template run_in_row<Required>()), "run_in_row<R>() is noexcept, matching run().");

    // A pointer-to-member type encodes every one of those axes at once.
    static_assert(std::is_same_v<decltype(&BackgroundThread::template run_in_row<Required>),
                                 void (BackgroundThread::*)() noexcept>);

    static_assert(std::is_same_v<decltype(&BackgroundThread::template run_in_row<eff::AllRow>),
                                 void (BackgroundThread::*)() noexcept>);

    // Every instantiation has that same shape, so the row parameter never
    // leaks into the function type.
    using SuperRow = eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
                              eff::Effect::Init, eff::Effect::Test>;
    static_assert(std::is_same_v<decltype(&BackgroundThread::template run_in_row<SuperRow>),
                                 void (BackgroundThread::*)() noexcept>);

    // The two instantiations sit at different addresses but share one type,
    // so a caller can dispatch to either through the same slot.
    constexpr auto p1 = &BackgroundThread::template run_in_row<Required>;
    constexpr auto p2 = &BackgroundThread::template run_in_row<eff::AllRow>;
    static_assert(std::is_same_v<decltype(p1), decltype(p2)>);

    std::printf("  audit-H forwarder_fidelity_signature:      PASSED\n");
}

// A batch large enough to span several drain batches, to catch a loop that
// stops before consuming everything it drained.
//
// The hashes are all distinct so the iteration detector never fires. Taking
// the boundary path would need the global schema and kernel tables sealed,
// which the normal start sequence does and this test deliberately bypasses in
// order to reach the row-typed entry point directly.
static void test_audit_i_large_batch_drain() {
    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    bt.ring.set(BackgroundThread::RingPtr{ring.get()});
    bt.meta_log.set(BackgroundThread::MetaLogPtr{metalog.get()});
    bt.stop_requested.reset_in_quiescent_context(crucible::safety::OneShotFlag::QuiescenceProof{});

    using Required = BackgroundThread::run_required_row;

    std::jthread bg_thread([&]() { bt.run_in_row<Required>(); });

    constexpr uint32_t TOTAL = 32;
    for (uint32_t i = 0; i < TOTAL; ++i) {
        crucible::TraceRing::Entry e{};
        e.schema_hash = crucible::SchemaHash{0xABCD0001ULL + i};
        e.shape_hash = crucible::ShapeHash{0xDEAD0001ULL + i};
        while (
            !ring->try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})
                 .peek()) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bt.total_processed.load() < TOTAL && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    const uint64_t processed_after_push = bt.total_processed.load();
    assert(processed_after_push >= TOTAL);

    bt.stop_requested.signal();
    bg_thread.join();

    // No signature ever matched twice, so the boundary path never ran and the
    // completed count stays at zero.
    assert(bt.iterations_completed.get() == 0u);

    std::printf("  audit-I large_batch_drain:                 "
                "PASSED (processed=%llu of %u)\n",
                static_cast<unsigned long long>(processed_after_push), TOTAL);
}

// Spawn, stop, spawn again. A once-flag, a static guard inside the template
// instantiation, or a destructor that failed to run would leave the second
// invocation dead, and the second cycle would then drain nothing.
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
        bt.stop_requested.reset_in_quiescent_context(crucible::safety::OneShotFlag::QuiescenceProof{});

        std::jthread bg([&]() { bt.run_in_row<Required>(); });

        for (uint32_t i = 0; i < PER_CYCLE; ++i) {
            crucible::TraceRing::Entry e{};
            // The cycle index enters the hash so the detector cannot tie the
            // two cycles together into one signature.
            e.schema_hash = crucible::SchemaHash{0x10000ULL + (cycle << 16) + i};
            e.shape_hash = crucible::ShapeHash{0x20000ULL + i};
            while (!ring->try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0},
                                            crucible::CallsiteHash{0})
                        .peek()) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }

        const uint64_t target = prev_processed + PER_CYCLE;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (bt.total_processed.load() < target && std::chrono::steady_clock::now() < deadline) {
            CRUCIBLE_SPIN_PAUSE;
        }
        assert(bt.total_processed.load() >= target);

        bt.stop_requested.signal();
        bg.join();

        prev_processed = bt.total_processed.load();
    }

    // Both invocations consumed entries, so the total covers both cycles.
    assert(bt.total_processed.load() >= 2 * PER_CYCLE);

    std::printf("  audit-J rearm_cycle:                       "
                "PASSED (total=%llu over 2 cycles)\n",
                static_cast<unsigned long long>(bt.total_processed.load()));
}

// ── audit-K / audit-L: divergence reset and the publish stage ───────────
//
// These two share a rig, so it is described once here.
//
// A region-ready callback that blocks is the lever.  The publish stage
// cannot leave the callback until the test lets it, which pins the pipeline
// at a known point and turns two races into an ordered sequence.
//
// The cost is that the block has to be safe.  A callback that blocked while
// holding the arena gate would deadlock the build stage behind it, so the
// rig is also a live check that the publish path holds nothing the rest of
// the pipeline needs.
namespace publish_rig {

struct Gate {
    BackgroundThread* owner = nullptr;

    // Raised by the publish stage when it enters the callback, lowered by
    // the test when it wants the stage to continue.
    std::atomic<bool> in_callback{false};
    std::atomic<bool> release{false};
    std::atomic<bool> hold_next{true};

    // The arena-gate probe, run on the first callback only.
    std::atomic<bool> probe_done{false};
    std::atomic<bool> gate_was_free{false};

    // Set by the test once it has watched the detect stage consume the
    // reset.  Every publish after that point is a post-reset publish.
    std::atomic<bool> reset_observed{false};

    std::atomic<uint32_t> published{0};
    std::atomic<uint32_t> published_after_reset{0};
    std::atomic<uint32_t> stale_after_reset{0};
    std::atomic<uint32_t> hash_mismatches{0};

    // Which op family the region's first op came from.  Family A is
    // everything recorded before the reset.
    static constexpr uint64_t FAMILY_A = 0x00A00000ULL;
    static constexpr uint64_t FAMILY_B = 0x00B00000ULL;
    static constexpr uint64_t FAMILY_MASK = 0x00FF0000ULL;
};

static void on_region_ready(void* ctx, crucible::RegionNode* region) noexcept {
    auto* gate = static_cast<Gate*>(ctx);

    // The hash the background thread folded while streaming the ops must
    // equal the hash the canonical span fold produces over the same ops.
    // The two used to spell the seed and the finalizer separately, so this
    // is the assertion that binds the production producer to the canonical
    // one on a region that really came off the pipeline.
    const crucible::ContentHash canonical =
        crucible::compute_content_hash(std::span<const crucible::TraceEntry>{region->ops, region->num_ops});
    if (canonical != region->content_hash) gate->hash_mismatches.fetch_add(1, std::memory_order_relaxed);

    // The callback must not run inside the arena gate.  The gate is not
    // recursive, so a stage that still held it across this call could never
    // take it here no matter how long it waited: a successful acquire is
    // proof the publish path let go before calling out.  The deadline
    // absorbs the build stage legitimately holding the gate mid-allocation.
    if (gate->owner != nullptr && !gate->probe_done.exchange(true, std::memory_order_acq_rel)) {
        const auto probe_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        bool took = false;
        while (!(took = gate->owner->arena_alloc_gate_.try_lock())
               && std::chrono::steady_clock::now() < probe_deadline) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (took) gate->owner->arena_alloc_gate_.unlock();
        gate->gate_was_free.store(took, std::memory_order_release);
    }

    const bool after_reset = gate->reset_observed.load(std::memory_order_acquire);
    const uint64_t family = region->ops[0].schema_hash.raw() & Gate::FAMILY_MASK;

    gate->published.fetch_add(1, std::memory_order_release);
    if (after_reset) {
        gate->published_after_reset.fetch_add(1, std::memory_order_release);
        if (family == Gate::FAMILY_A) gate->stale_after_reset.fetch_add(1, std::memory_order_release);
    }

    if (!gate->hold_next.load(std::memory_order_acquire)) return;
    gate->hold_next.store(false, std::memory_order_release);
    gate->in_callback.store(true, std::memory_order_release);
    while (!gate->release.load(std::memory_order_acquire)) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

static void push(crucible::TraceRing& ring, uint64_t family, uint32_t op) {
    crucible::TraceRing::Entry e{};
    e.schema_hash = crucible::SchemaHash{family | op};
    e.shape_hash = crucible::ShapeHash{0x5000ULL + op};
    while (!ring.try_append_pinned(e, crucible::MetaIndex::none(), crucible::ScopeHash{0}, crucible::CallsiteHash{0})
                .peek()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Enough repeats of one signature for the detector to confirm it and then
// report boundaries.  The first full re-match only confirms; boundaries
// start at the one after.
static constexpr uint32_t OPS_PER_ITER = 8;
static constexpr uint32_t A_ITERS = 6;

}  // namespace publish_rig

// A divergence reset must drop the work the pipeline is already carrying.
//
// Without that, a region cut entirely from pre-divergence entries finishes
// its build and publishes after the reset.  The foreground has just
// deactivated replay and gone back to recording, and this publish hands it
// back the shape that diverged.
//
// The sequence the rig makes deterministic:
//   1. family-A ops flow until the first region reaches the callback, which
//      blocks there with more A regions queued behind it
//   2. the test signals the reset and pushes family-B ops, which is what
//      wakes the detect stage so it can consume the signal
//   3. the test waits for the signal to clear, which is the detect stage
//      saying it ran the reset
//   4. the callback is released, and the queued A regions drain
// A family-A region published after step 3 is the defect.
static void test_audit_k_reset_drops_inflight_regions() {
    using namespace publish_rig;

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    Gate gate;
    gate.owner = &bt;

    bt.set_region_ready_callback(&gate, &on_region_ready);
    bt.start(ring.get(), metalog.get());

    for (uint32_t iter = 0; iter < A_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(*ring, Gate::FAMILY_A, op);

    // Wait for the publish stage to be parked in the callback with at least
    // two boundaries behind it, so there is work in flight to drop.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((!gate.in_callback.load(std::memory_order_acquire) || bt.iterations_completed.get() < 3)
           && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(gate.in_callback.load(std::memory_order_acquire) && "publish stage never reached the callback");
    assert(bt.iterations_completed.get() >= 3 && "detector never cut enough work to leave any in flight");
    const uint32_t published_before_reset = gate.published.load(std::memory_order_acquire);
    assert(published_before_reset == 1 && "the gate should hold the pipeline at the first publish");

    // The foreground's divergence handler does exactly this.
    bt.reset_requested.signal();

    // The detect stage only looks at the flag when a batch arrives, so the
    // signal needs traffic behind it.
    for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
        push(*ring, Gate::FAMILY_B, op);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (bt.reset_requested.peek() && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(!bt.reset_requested.peek() && "detect stage never consumed the reset");

    // Nothing can have published while the stage was parked, so the counter
    // is still where it was and the marker below cannot race a publish.
    assert(gate.published.load(std::memory_order_acquire) == published_before_reset);
    gate.reset_observed.store(true, std::memory_order_release);

    gate.release.store(true, std::memory_order_release);

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const uint64_t target = ring->total_produced();
    while (bt.total_processed.get() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    // The commit markers have to keep flowing past a dropped region, or a
    // foreground flush() would never return.
    assert(bt.total_processed.get() >= target && "commit markers stalled behind a dropped region");

    bt.stop();

    assert(gate.hash_mismatches.load() == 0
           && "streaming content hash disagreed with the canonical span fold on a published region");

    const uint32_t stale = gate.stale_after_reset.load();
    // stderr, because the assert below aborts and stdout is block-buffered
    // into a pipe, so a printf here would be lost exactly when it matters.
    if (stale != 0)
        std::fprintf(stderr,
                     "  audit-K FAILED: %u of %u post-reset publishes carried "
                     "pre-divergence ops (cut=%u)\n",
                     stale, gate.published_after_reset.load(), bt.iterations_completed.get());
    assert(stale == 0 && "a region built from pre-divergence entries published after the reset");

    std::printf("  audit-K reset_drops_inflight_regions:      "
                "PASSED (published=%u, after reset=%u, stale=%u, cut=%u)\n",
                gate.published.load(), gate.published_after_reset.load(), stale, bt.iterations_completed.get());
}

// The region-ready callback must not run inside the arena gate.
//
// It used to.  The callback commits a transaction, flips the mode cell and
// reads sampled counters, and the build stage was shut out of the arena for
// the whole of it.  The gate spins on a budget and then yields, so the build
// stage reached the kernel every time the callback ran long.
//
// The probe inside the callback is what decides it, and the deadline there
// is what makes the verdict unambiguous: the gate is not recursive, so a
// publish stage still holding it could not take it again however long it
// waited.
static void test_audit_l_callback_runs_outside_arena_gate() {
    using namespace publish_rig;

    BackgroundThread bt;
    auto ring = std::make_unique<TraceRing>();
    auto metalog = std::make_unique<MetaLog>();
    Gate gate;
    gate.owner = &bt;
    // Nothing is parked here.  Holding the callback would only keep the
    // pipeline waiting on a verdict the first callback already reached.
    gate.hold_next.store(false, std::memory_order_release);

    bt.set_region_ready_callback(&gate, &on_region_ready);
    bt.start(ring.get(), metalog.get());

    for (uint32_t iter = 0; iter < A_ITERS; ++iter)
        for (uint32_t op = 0; op < OPS_PER_ITER; ++op)
            push(*ring, Gate::FAMILY_A, op);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const uint64_t target = ring->total_produced();
    while (bt.total_processed.get() < target && std::chrono::steady_clock::now() < deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    assert(bt.total_processed.get() >= target);

    bt.stop();

    assert(gate.probe_done.load() && "no region published, so the gate was never probed");
    assert(gate.gate_was_free.load() && "the region-ready callback ran while the arena gate was held");
    assert(gate.hash_mismatches.load() == 0);

    // The queue is bounded.  This run is shorter than the bound, so the
    // assertion is on the relationship rather than on a constant; audit-M
    // drives the wrap itself.
    assert(bt.uncompiled_regions.size() <= BackgroundThread::UncompiledRegionQueue::CAP);
    assert(bt.uncompiled_regions.size()
           == std::min<uint64_t>(bt.uncompiled_regions.total(), BackgroundThread::UncompiledRegionQueue::CAP));

    std::printf("  audit-L callback_outside_arena_gate:       "
                "PASSED (gate free in callback, retained=%u of %llu)\n",
                bt.uncompiled_regions.size(), static_cast<unsigned long long>(bt.uncompiled_regions.total()));
}

// The retained-region queue is bounded, so a process that publishes forever
// holds a constant number of pointers rather than one per region.  Driving
// past the bound directly is the only way to see the wrap.
static void test_audit_m_uncompiled_queue_is_bounded() {
    BackgroundThread::UncompiledRegionQueue queue;
    constexpr uint32_t CAP = BackgroundThread::UncompiledRegionQueue::CAP;

    assert(queue.size() == 0u);
    assert(queue.total() == 0u);

    // The pointers are never dereferenced by the queue, so distinct
    // non-null bit patterns stand in for regions.
    auto fake = [](uint64_t i) { return std::bit_cast<crucible::RegionNode*>(uintptr_t{0x1000} + i * 0x40); };

    for (uint64_t i = 0; i < CAP; ++i) {
        queue.push(fake(i));
        assert(queue.total() == i + 1);
        assert(queue.size() == i + 1);
        assert(queue.at(0) == fake(i) && "age 0 is the newest");
    }

    // One past the bound: the total keeps counting, the retention does not.
    constexpr uint64_t OVERRUN = 4u * CAP + 3u;
    for (uint64_t i = CAP; i < OVERRUN; ++i)
        queue.push(fake(i));

    assert(queue.total() == OVERRUN);
    assert(queue.size() == CAP && "retention must stop at the bound");
    for (uint32_t age = 0; age < CAP; ++age)
        assert(queue.at(age) == fake(OVERRUN - 1 - age) && "the bound keeps the newest CAP, in order");

    std::printf("  audit-M uncompiled_queue_bounded:          "
                "PASSED (pushed=%llu, retained=%u)\n",
                static_cast<unsigned long long>(queue.total()), queue.size());
}

int main() {
    std::printf("test_background_thread_run_in_row — effect-row fence\n");
    test_t01_required_row_pinned();
    test_t02_subrow_accepted_shapes();
    test_t03_subrow_rejected_shapes();
    test_t04_api_surface_pinned();
    test_t05_runtime_smoke();
    test_t06_f_star_alias_all_row();
    test_t07_multi_row_caller();

    std::printf("--- audit groups ---\n");
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
    // These three run last because start() seals the global schema and
    // kernel tables, which every group above deliberately avoids.
    test_audit_m_uncompiled_queue_is_bounded();
    test_audit_k_reset_drops_inflight_regions();
    test_audit_l_callback_runs_outside_arena_gate();

    std::printf("test_background_thread_run_in_row: 7 + 13 audit "
                "groups, all passed\n");
    return 0;
}
