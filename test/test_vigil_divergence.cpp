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

// The other half of the same defect: the alignment walk, rather than the
// publication slot.
//
// A walk lives in two foreground members that no divergence used to clear.
// rollback() activates a context without looking at them, so a walk that is
// half done survives into compiled mode, and the divergence that follows
// leaves it there.  The next dispatch then resumes a walk into a region the
// divergence abandoned, and records nothing while it does.
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
    constexpr uint32_t MAX_HELD = crucible::BackgroundThread::UncompiledRegionQueue::CAP + 16 + 10 + 2;

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

}  // namespace test_vigil
