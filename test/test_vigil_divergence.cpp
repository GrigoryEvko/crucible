// The rig and the divergence tests of test_vigil: a divergence drops what
// the foreground has not observed.

#include "vigil_rig.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstdint>
#include <cstdio>

namespace divrig {

namespace {

void* fake_ptr(uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

crucible::TensorMeta meta_at(void* data_ptr) {
    crucible::TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(1024);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx = 0;
    m.layout = crucible::Layout::Strided;
    m.data_ptr = crucible::external_data_ptr(data_ptr);
    return m;
}

}  // namespace

OpData op_at(uint32_t iter, uint32_t op_idx) {
    OpData made;
    made.entry.schema_hash = SCHEMA[op_idx];
    made.entry.shape_hash = SHAPE[op_idx];
    made.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    made.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0) made.metas[idx++] = meta_at(fake_ptr(iter, op_idx - 1));
    made.metas[idx++] = meta_at(fake_ptr(iter, op_idx));
    made.n_metas = static_cast<uint16_t>(made.entry.num_inputs + made.entry.num_outputs);
    return made;
}

void feed_one_region(crucible::Vigil& vigil, uint32_t first_iter) {
    for (uint32_t iter = first_iter; iter < first_iter + 2; iter++) {
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto recorded = op_at(iter, i);
            (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                                  recorded.n_metas);
        }
    }
    for (uint32_t i = 0; i < K; i++) {
        auto recorded = op_at(first_iter + 2, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                              recorded.n_metas);
    }
}

}  // namespace divrig

namespace test_vigil {

void test_divergence_drops_the_unobserved_region() {
    using namespace divrig;
    using crucible::DispatchResult;
    using crucible::ReplayStatus;

    crucible::Vigil vigil;

    // Phase 1: the background thread publishes a region and the foreground
    // aligns onto it, which leaves the context compiled.
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);

    for (uint32_t i = 0; i < K; i++) {
        auto recorded = op_at(3, i);
        auto result = crucible::test::dispatch_synthetic(vigil, recorded.entry, recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD && "alignment ops return RECORD");
    }
    assert(vigil.context().is_compiled() && "the context must be compiled after K alignment ops");
    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto recorded = op_at(3, i);
        auto result = crucible::test::dispatch_synthetic(vigil, recorded.entry, recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);
    }

    const uint64_t step_after_first = vigil.current_step();
    assert(step_after_first >= 1 && "the background thread must have published one region");

    // Phase 2: open the window.  These entries reach the ring while the
    // context is compiled, so the region the background thread publishes for
    // them lands in a slot the replay branch of dispatch_op never reads.
    feed_one_region(vigil, 4);
    vigil.flush();
    assert(vigil.flush_complete() && "flush() returned before the background thread finished");
    assert(vigil.current_step() > step_after_first
           && "the background thread must have published a second region into the unobserved slot");
    assert(vigil.context().is_compiled() && "the foreground must still be compiled: it never read the slot");

    // Phase 3: diverge.  The diverging op is not recorded, and the ring is
    // empty, so the background thread cannot publish anything between here
    // and the dispatch below.
    const uint32_t diverged_before = vigil.diverged_count();
    OpData foreign_op = op_at(7, 0);
    foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
    auto result_diverged =
        crucible::test::dispatch_synthetic(vigil, foreign_op.entry, foreign_op.metas, foreign_op.n_metas);
    assert(result_diverged.action == DispatchResult::Action::RECORD && "a divergence falls back to recording");
    assert(result_diverged.status == ReplayStatus::DIVERGED);
    assert(vigil.diverged_count() > diverged_before);
    assert(!vigil.context().is_compiled() && "the context must be deactivated by the divergence");

    // The claim.  The next op belongs to a trace the background thread has
    // not seen yet, so it must reach the ring.  If the unobserved region is
    // still in the slot, this op is consumed by alignment instead and the
    // ring never grows.
    const uint64_t produced_before = vigil.ring_total_produced();
    auto after_divergence = op_at(7, 0);
    auto result_after = crucible::test::dispatch_synthetic(vigil, after_divergence.entry, after_divergence.metas,
                                                           after_divergence.n_metas);
    assert(result_after.action == DispatchResult::Action::RECORD);
    assert(vigil.ring_total_produced() == produced_before + 1
           && "the first op after a divergence must be recorded, not aligned against the region the "
              "background thread published while the context was compiled");

    crucible::test::pass("  test_divergence_drops_the_unobserved_region: PASSED\n");
}

// The same claim, with ops in flight in the background pipeline at the
// divergence.
//
// The ops of the backlog reach the ring while the context is compiled, and
// the divergence comes before the background thread has read them.  Each op
// of the backlog was recorded before the divergence, so no region of the
// backlog holds an op of the trace after it.  After the flush, the slot must
// give the foreground no such region: the first op after the divergence must
// reach the ring.
void test_divergence_drops_the_regions_of_the_backlog() {
    using namespace divrig;
    using crucible::DispatchResult;
    using crucible::ReplayStatus;
    // Each iteration of the backlog completes a boundary.  The region store
    // holds each region of the backlog, because the foreground writes no
    // holders while it replays.
    constexpr uint32_t BACKLOG_ITERATIONS = 48;

    crucible::Vigil vigil;
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto aligned = op_at(3, i);
        (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
    }
    assert(vigil.context().is_compiled() && "the foreground must align on the first region");

    for (uint32_t iter = 4; iter < 4 + BACKLOG_ITERATIONS; iter++) {
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto recorded = op_at(iter, i);
            (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                                  recorded.n_metas);
        }
    }

    OpData foreign_op = op_at(4 + BACKLOG_ITERATIONS, 0);
    foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
    auto result_diverged =
        crucible::test::dispatch_synthetic(vigil, foreign_op.entry, foreign_op.metas, foreign_op.n_metas);
    assert(result_diverged.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    vigil.flush();
    assert(vigil.flush_complete() && "flush() returned before the background thread finished");

    const uint64_t produced_before = vigil.ring_total_produced();
    auto after_divergence = op_at(5 + BACKLOG_ITERATIONS, 0);
    auto result_after = crucible::test::dispatch_synthetic(vigil, after_divergence.entry, after_divergence.metas,
                                                           after_divergence.n_metas);
    assert(result_after.action == DispatchResult::Action::RECORD);
    assert(vigil.ring_total_produced() == produced_before + 1
           && "the first op after a divergence must be recorded, not aligned against a region of ops recorded "
              "before the divergence");

    crucible::test::pass("  test_divergence_drops_the_regions_of_the_backlog: PASSED\n");
}

// The other half of the same defect: the alignment walk, rather than the
// publication slot.
//
// A walk lives in foreground members that a divergence clears.  A walk that
// stays open into compiled mode lets the next dispatch after a divergence
// resume a walk into a region that the divergence abandoned, and record
// nothing while it does.  The rollback closes the walk too, so the walk here
// is closed twice.
//
// The walk is opened here by dispatching two ops of a published region and
// stopping short of the K that would activate it.  rollback() then supplies
// the compiled context, which is what makes a divergence reachable at all.
void test_divergence_drops_a_half_finished_alignment() {
    using namespace divrig;
    using crucible::DispatchResult;
    using crucible::ReplayStatus;

    crucible::Vigil vigil;

    // Two published regions, so the transaction log has one superseded
    // entry for rollback() to restore.  Neither is observed by the
    // foreground: these entries reach the ring through record_op.
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);
    const uint64_t step_after_first = vigil.current_step();

    feed_one_region(vigil, 4);
    vigil.flush();
    assert(vigil.current_step() > step_after_first && "a second region must be published");

    // Open a walk two ops deep and stop.  K is 5, so this does not activate.
    for (uint32_t i = 0; i < 2; i++) {
        auto recorded = op_at(8, i);
        auto result = crucible::test::dispatch_synthetic(vigil, recorded.entry, recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD);
    }
    assert(!vigil.context().is_compiled() && "two ops are short of the K that activates");

    // Compile the context out from under the open walk.
    assert(vigil.rollback() && "rollback() needs the superseded transaction the second region left");
    assert(vigil.context().is_compiled() && "rollback() must activate the restored region");

    OpData foreign_op = op_at(9, 0);
    foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
    auto result_diverged =
        crucible::test::dispatch_synthetic(vigil, foreign_op.entry, foreign_op.metas, foreign_op.n_metas);
    assert(result_diverged.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    // The claim.  This op continues the abandoned walk if the walk is still
    // open, and reaches the ring if it is not.
    const uint64_t produced_before = vigil.ring_total_produced();
    auto after_divergence = op_at(9, 2);
    auto result_after = crucible::test::dispatch_synthetic(vigil, after_divergence.entry, after_divergence.metas,
                                                           after_divergence.n_metas);
    assert(result_after.action == DispatchResult::Action::RECORD);
    assert(vigil.ring_total_produced() == produced_before + 1
           && "the first op after a divergence must be recorded, not fed to an alignment walk the "
              "divergence abandoned");

    crucible::test::pass("  test_divergence_drops_a_half_finished_alignment: PASSED\n");
}

namespace {

// An op with no tensors.  A region of such ops has no memory plan, so the
// context cannot replay it, and a rollback to it leaves the context
// recording.
crucible::TraceRing::Entry planless_entry(uint32_t op_idx) {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x700 + op_idx};
    entry.shape_hash = crucible::ShapeHash{0x800 + op_idx};
    return entry;
}

// The input of feed_one_region, with the ops of planless_entry.
void feed_planless_region(crucible::Vigil& vigil) {
    for (uint32_t iter = 0; iter < 3; iter++) {
        const uint32_t count = (iter < 2) ? divrig::NUM_OPS : divrig::K;
        for (uint32_t i = 0; i < count; i++) {
            const crucible::TraceRing::Entry entry = planless_entry(i);
            (void)vigil.record_op(crucible::test::certify_synthetic_entry(entry), nullptr, 0);
        }
    }
}

// Two published regions: one with no memory plan, then one with tensors.  The
// transaction of the second supersedes the first, so a rollback restores the
// region with no plan.  The foreground dispatches nothing, so the slot holds
// the second region.
void publish_planless_then_planned(crucible::Vigil& vigil) {
    feed_planless_region(vigil);
    crucible::test::flush_and_wait_region_published(vigil);
    const uint64_t step_after_planless = vigil.current_step();
    divrig::feed_one_region(vigil, 0);
    vigil.flush();
    assert(vigil.current_step() > step_after_planless && "the region with tensors must be published");
}

}  // namespace

// A rollback makes each region that the pipeline cut before it stale.
//
// The slot holds the region that the rollback rolls back.  The context
// records after the rollback, so the next dispatch reads the slot.  It must
// not align on that region: each op after the rollback reaches the ring.
void test_rollback_drops_the_region_in_the_slot() {
    using namespace divrig;
    using crucible::DispatchResult;

    crucible::Vigil vigil;
    publish_planless_then_planned(vigil);
    assert(vigil.has_pending_region());

    assert(vigil.rollback() && "the second region left a superseded transaction");
    assert(!vigil.context().is_compiled() && "a region with no memory plan cannot replay");

    const uint64_t produced_before = vigil.ring_total_produced();
    for (uint32_t i = 0; i < K; i++) {
        auto recorded = op_at(3, i);
        auto result = crucible::test::dispatch_synthetic(vigil, recorded.entry, recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD);
    }
    assert(!vigil.context().is_compiled() && "the foreground replays the region that the rollback rolled back");
    assert(vigil.ring_total_produced() == produced_before + K
           && "each op after a rollback must reach the ring, not an alignment on a region cut before it");

    crucible::test::pass("  test_rollback_drops_the_region_in_the_slot: PASSED\n");
}

// A rollback closes an open alignment walk.
//
// The walk is two ops deep in the region that the rollback rolls back.  The
// context records after the rollback, so a walk that stays open goes on with
// the next ops and activates that region.
void test_rollback_closes_an_open_alignment() {
    using namespace divrig;
    using crucible::DispatchResult;

    crucible::Vigil vigil;
    publish_planless_then_planned(vigil);
    for (uint32_t i = 0; i < 2; i++) {
        auto recorded = op_at(3, i);
        auto result = crucible::test::dispatch_synthetic(vigil, recorded.entry, recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD);
    }
    assert(!vigil.has_pending_region() && "the first dispatch takes the region and opens the walk");

    assert(vigil.rollback() && "the second region left a superseded transaction");
    assert(!vigil.context().is_compiled() && "a region with no memory plan cannot replay");

    const uint64_t produced_before = vigil.ring_total_produced();
    for (uint32_t i = 2; i < K; i++) {
        auto recorded = op_at(3, i);
        auto result = crucible::test::dispatch_synthetic(vigil, recorded.entry, recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD);
    }
    assert(!vigil.context().is_compiled() && "the walk that the rollback abandoned activated its region");
    assert(vigil.ring_total_produced() == produced_before + (K - 2)
           && "each op after a rollback must reach the ring, not the walk that the rollback abandoned");

    crucible::test::pass("  test_rollback_closes_an_open_alignment: PASSED\n");
}

namespace {

// The op of the rig, with schema hashes that no other cycle uses.  A
// divergence refutes the period of the region that diverged, with its body,
// so each cycle gives the detector a new body.  Each region of a cycle then
// has its own content hash too, and the region cache takes a new entry.
divrig::OpData cycle_op(uint32_t cycle, uint32_t iter, uint32_t op_idx) {
    divrig::OpData made = divrig::op_at(iter, op_idx);
    made.entry.schema_hash = crucible::SchemaHash{divrig::SCHEMA[op_idx].raw() + (uint64_t{cycle} + 1) * 0x10000};
    return made;
}

void record_cycle_op(crucible::Vigil& vigil, uint32_t cycle, uint32_t iter, uint32_t op_idx) {
    divrig::OpData made = cycle_op(cycle, iter, op_idx);
    (void)vigil.record_op(crucible::test::certify_synthetic_entry(made.entry), made.metas, made.n_metas);
}

// The input of feed_one_region, with the ops of the cycle.
void feed_cycle_region(crucible::Vigil& vigil, uint32_t cycle, uint32_t first_iter) {
    for (uint32_t iter = first_iter; iter < first_iter + 2; iter++) {
        for (uint32_t i = 0; i < divrig::NUM_OPS; i++)
            record_cycle_op(vigil, cycle, iter, i);
    }
    for (uint32_t i = 0; i < divrig::K; i++)
        record_cycle_op(vigil, cycle, first_iter + 2, i);
}

}  // namespace

// A long run of shape changes keeps the region store bounded.  Each cycle
// publishes a region and aligns on it, records the next region while the
// context is compiled, and diverges.  So each cycle publishes two regions,
// and the run publishes more regions than the store holds.  The sweep
// releases each region that no holder names.  The foreground replays the
// regions that it holds, so a region that the sweep releases too early gives
// a read after the release, which AddressSanitizer reports.
void test_region_store_stays_bounded() {
    using namespace divrig;
    constexpr uint32_t CYCLES = 120;
    // The uncompiled queue, the transaction log, the cells of the foreground,
    // and the last published region with the handed region.
    constexpr uint32_t MAX_HELD = crucible::BackgroundThread::UncompiledRegionQueue::CAP + 16 + 11 + 2;

    crucible::Vigil vigil;
    uint32_t base = 0;
    for (uint32_t cycle = 0; cycle < CYCLES; ++cycle) {
        feed_cycle_region(vigil, cycle, base);
        crucible::test::flush_and_wait_region_published(vigil);
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            OpData aligned = cycle_op(cycle, base + 3, i);
            (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
        }
        assert(vigil.context().is_compiled() && "each cycle aligns on its region");

        feed_cycle_region(vigil, cycle, base + 4);
        vigil.flush();
        OpData foreign_op = op_at(base + 7, 0);
        foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
        auto diverged =
            crucible::test::dispatch_synthetic(vigil, foreign_op.entry, foreign_op.metas, foreign_op.n_metas);
        assert(diverged.status == crucible::ReplayStatus::DIVERGED);
        base += 8;
    }
    vigil.flush();

    const uint32_t stored = vigil.region_store_count();
    const uint64_t dropped = vigil.region_store_drops();
    assert(vigil.current_step() >= 2 * CYCLES && "the run published too few regions");
    assert(dropped == 0 && "a full region store discarded a published region");
    assert(stored <= MAX_HELD && "the region store kept a region that no holder names");

    crucible::test::pass("  test_region_store_stays_bounded: PASSED (published={}, stored={})\n",
                         static_cast<unsigned long long>(vigil.current_step()), stored);
}

// The active region names no region that a reset made stale.
//
// The foreground replays a region of the first cycle.  The ops of a second
// cycle reach the ring while it replays, so the background publishes their
// region into the slot, and that region is the active region.  A divergence
// makes it stale, because its ops came before the divergence.  The active
// region is then the region that the foreground took last.
void test_divergence_leaves_no_stale_active_region() {
    using namespace divrig;

    crucible::Vigil vigil;
    feed_cycle_region(vigil, 0, 0);
    crucible::test::flush_and_wait_region_published(vigil);
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        OpData aligned = cycle_op(0, 3, i);
        (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
    }
    assert(vigil.context().is_compiled() && "the foreground aligns on the region of the first cycle");
    const crucible::ContentHash replayed_hash = vigil.context().active_region()->content_hash;
    assert(vigil.active_region()->content_hash == replayed_hash);

    const uint64_t step_before = vigil.current_step();
    feed_cycle_region(vigil, 1, 4);
    vigil.flush();
    assert(vigil.current_step() > step_before && "the region of the second cycle must be published");
    assert(vigil.active_region()->content_hash != replayed_hash && "the slot holds the newest region");

    OpData foreign_op = op_at(7, 0);
    foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
    auto diverged = crucible::test::dispatch_synthetic(vigil, foreign_op.entry, foreign_op.metas, foreign_op.n_metas);
    assert(diverged.status == crucible::ReplayStatus::DIVERGED);
    assert(vigil.active_region()->content_hash == replayed_hash
           && "the active region names the region of ops recorded before the divergence");

    crucible::test::pass("  test_divergence_leaves_no_stale_active_region: PASSED\n");
}

// A newer region with the same ops keeps an open alignment walk.
//
// The walk is two ops deep in the first region.  The ops that reach the ring
// through record_op then complete one more iteration, so the background
// publishes a second region with the same content.  The walk goes on in the
// second region, and the K-th op activates it.
void test_same_region_keeps_the_alignment() {
    using namespace divrig;

    crucible::Vigil vigil;
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);
    for (uint32_t i = 0; i < 2; i++) {
        OpData aligned = op_at(3, i);
        (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
    }
    assert(!vigil.context().is_compiled() && "two ops are short of the K that activates");

    const uint64_t step_before = vigil.current_step();
    for (uint32_t i = K; i < NUM_OPS; i++) {
        OpData recorded = op_at(2, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                              recorded.n_metas);
    }
    for (uint32_t i = 0; i < K; i++) {
        OpData recorded = op_at(3, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                              recorded.n_metas);
    }
    vigil.flush();
    assert(vigil.current_step() > step_before && "the second region must be published");
    assert(vigil.has_pending_region());

    for (uint32_t i = 2; i < K; i++) {
        OpData aligned = op_at(3, i);
        (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
    }
    assert(vigil.context().is_compiled() && "a region with the same ops restarted the walk from zero");

    crucible::test::pass("  test_same_region_keeps_the_alignment: PASSED\n");
}

}  // namespace test_vigil
