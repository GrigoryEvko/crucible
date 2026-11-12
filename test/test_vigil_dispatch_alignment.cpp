// The alignment of test_vigil_dispatch: the foreground matches the head of a
// published region before replay activates.  The tests are deterministic.
// They do not depend on the speed of the background thread, because each one
// waits for the publication before it dispatches.

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>

using namespace crucible;

namespace test_vigil_dispatch {

using crucible::test::flush_and_wait_region_published;

namespace {

// The ops A that start the iteration with a border, before the op B.
inline constexpr uint32_t BORDER_RUN = 4;

// Op op_idx of the iteration A A A A B C D E.  The data pointers are those of
// make_op, and each A has one schema hash and one shape hash.
OpData bordered_op(uint32_t iter, uint32_t op_idx) {
    OpData d = make_op(iter, op_idx);
    const uint64_t code = op_idx < BORDER_RUN ? 0 : op_idx - BORDER_RUN + 1;
    d.entry.schema_hash = SchemaHash{0x500 + code};
    d.entry.shape_hash = ShapeHash{0x600 + code};
    return d;
}

void record_bordered(Vigil& vigil, uint32_t iter, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        OpData d = bordered_op(iter, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(d.entry), d.metas, d.n_metas);
    }
}

[[nodiscard]] DispatchResult dispatch_bordered(Vigil& vigil, uint32_t iter, uint32_t op_idx) {
    OpData d = bordered_op(iter, op_idx);
    return crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
}

// An op that no region of these tests holds.
[[nodiscard]] DispatchResult dispatch_foreign(Vigil& vigil, uint32_t iter) {
    OpData d = make_op(iter, 0);
    d.entry.schema_hash = SchemaHash{0x900};
    d.entry.shape_hash = ShapeHash{0x900};
    return crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
}

// The iteration of make_op that the first op of the new trace uses.  It is
// after the iterations that the region of the old trace recorded.
inline constexpr uint32_t NEW_TRACE_ITER = 10;

// Op `index` of a new trace: iterations of NUM_OPS ops with the data flow of
// make_op, and hashes that the region of the old trace does not hold.
OpData new_trace_op(uint32_t index) {
    const uint32_t code = index % NUM_OPS;
    OpData d = make_op(NEW_TRACE_ITER + index / NUM_OPS, code);
    d.entry.schema_hash = SchemaHash{0xA00 + code};
    d.entry.shape_hash = ShapeHash{0xB00 + code};
    return d;
}

[[nodiscard]] DispatchResult dispatch_new_trace(Vigil& vigil, uint32_t index) {
    OpData d = new_trace_op(index);
    return crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
}

}  // namespace

// The head of the region is A A A A B, and the stream gives A A A A A B.  The
// fifth A does not match B.  The four ops before it still end with A A A,
// which with this A is a match of four, so the B after it completes the head.
void test_alignment_continues_inside_the_matched_ops() {
    Vigil vigil;

    record_bordered(vigil, 0, NUM_OPS);
    record_bordered(vigil, 1, NUM_OPS);
    record_bordered(vigil, 2, K);
    flush_and_wait_region_published(vigil);

    (void)dispatch_bordered(vigil, 3, 0);
    for (uint32_t i = 0; i < BORDER_RUN; i++) {
        const DispatchResult result = dispatch_bordered(vigil, 3, i);
        assert(result.action == DispatchResult::Action::RECORD && "an alignment op returns RECORD");
        assert(!vigil.is_compiled() && "no replay before the head of the region is complete");
    }
    (void)dispatch_bordered(vigil, 3, BORDER_RUN);
    assert(vigil.context().is_compiled() && "the B after the fifth A completes the head A A A A B");

    for (uint32_t i = BORDER_RUN + 1; i < NUM_OPS; i++) {
        const DispatchResult result = dispatch_bordered(vigil, 3, i);
        assert(result.action == DispatchResult::Action::COMPILED);
    }
    assert(vigil.compiled_iterations() == 1);

    crucible::test::pass("  test_alignment_continues_inside_the_matched_ops: PASSED\n");
}

// A stream that does not hold the region ends the alignment after
// Vigil::alignment_bound ops.  No op of the alignment goes to the ring, and
// the op after the bound does, so the background can publish a new region.
void test_alignment_ends_after_its_bound() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);
    flush_and_wait_region_published(vigil);

    const uint64_t produced = vigil.ring_total_produced();
    const uint32_t bound = Vigil::alignment_bound(NUM_OPS);
    for (uint32_t i = 0; i < bound; i++) {
        const DispatchResult result = dispatch_foreign(vigil, 3);
        assert(result.action == DispatchResult::Action::RECORD && "an alignment op returns RECORD");
        assert(vigil.ring_total_produced() == produced && "an op of the alignment goes to no ring");
    }
    (void)dispatch_foreign(vigil, 3);
    assert(vigil.ring_total_produced() == produced + 1 && "the op after the bound goes to the ring");
    assert(!vigil.is_compiled());

    crucible::test::pass("  test_alignment_ends_after_its_bound: PASSED\n");
}

// The stream changes to a new trace while the region of the old trace waits
// for alignment.  The bound ends the alignment, the background publishes a
// region of the new trace, and replay starts on it.  Before the flush, the
// test records only the ops that complete the first detection of the new
// trace, so the background publishes no region before the flush.
void test_alignment_bound_returns_to_replay() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);
    flush_and_wait_region_published(vigil);

    const uint32_t first_detection = Vigil::alignment_bound(NUM_OPS) + 2 * NUM_OPS + K;
    uint32_t index = 0;
    for (; index < first_detection; index++) {
        const DispatchResult result = dispatch_new_trace(vigil, index);
        assert(result.action == DispatchResult::Action::RECORD && "no replay before a region of the new trace");
    }
    flush_and_wait_region_published(vigil);

    assert(vigil.active_region() != nullptr);
    const RegionNode& region = *vigil.active_region();
    assert(region.num_ops == NUM_OPS && "the region holds one iteration of the new trace");
    for (uint32_t i = 0; i < region.num_ops; i++) {
        const uint64_t schema = region.ops[i].schema_hash.raw();
        assert(schema >= 0xA00 && schema < 0xA00 + NUM_OPS && "each op of the region is an op of the new trace");
    }

    const uint32_t replay_end = index + 3 * NUM_OPS;
    for (; index < replay_end; index++)
        (void)dispatch_new_trace(vigil, index);
    assert(vigil.is_compiled() && "replay starts on the region of the new trace");
    assert(vigil.compiled_iterations() >= 1);

    crucible::test::pass("  test_alignment_bound_returns_to_replay: PASSED\n");
}

}  // namespace test_vigil_dispatch
