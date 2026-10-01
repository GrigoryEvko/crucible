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
        auto result = vigil.dispatch_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                                        recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD && "alignment ops return RECORD");
    }
    assert(vigil.context().is_compiled() && "the context must be compiled after K alignment ops");
    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto recorded = op_at(3, i);
        auto result = vigil.dispatch_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                                        recorded.n_metas);
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
    auto result_diverged = vigil.dispatch_op(crucible::test::certify_synthetic_entry(foreign_op.entry),
                                             foreign_op.metas, foreign_op.n_metas);
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
    auto result_after = vigil.dispatch_op(crucible::test::certify_synthetic_entry(after_divergence.entry),
                                          after_divergence.metas, after_divergence.n_metas);
    assert(result_after.action == DispatchResult::Action::RECORD);
    assert(vigil.ring_total_produced() == produced_before + 1
           && "the first op after a divergence must be recorded, not aligned against the region the "
              "background thread published while the context was compiled");

    std::printf("  test_divergence_drops_the_unobserved_region: PASSED\n");
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
        auto result = vigil.dispatch_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                                        recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD);
    }
    assert(!vigil.context().is_compiled() && "two ops are short of the K that activates");

    // Compile the context out from under the open walk.
    assert(vigil.rollback() && "rollback() needs the superseded transaction the second region left");
    assert(vigil.context().is_compiled() && "rollback() must activate the restored region");

    OpData foreign_op = op_at(9, 0);
    foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
    auto result_diverged = vigil.dispatch_op(crucible::test::certify_synthetic_entry(foreign_op.entry),
                                             foreign_op.metas, foreign_op.n_metas);
    assert(result_diverged.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    // The claim.  This op continues the abandoned walk if the walk is still
    // open, and reaches the ring if it is not.
    const uint64_t produced_before = vigil.ring_total_produced();
    auto after_divergence = op_at(9, 2);
    auto result_after = vigil.dispatch_op(crucible::test::certify_synthetic_entry(after_divergence.entry),
                                          after_divergence.metas, after_divergence.n_metas);
    assert(result_after.action == DispatchResult::Action::RECORD);
    assert(vigil.ring_total_produced() == produced_before + 1
           && "the first op after a divergence must be recorded, not fed to an alignment walk the "
              "divergence abandoned");

    std::printf("  test_divergence_drops_a_half_finished_alignment: PASSED\n");
}

}  // namespace test_vigil
