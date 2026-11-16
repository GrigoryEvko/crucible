// The nesting depth of each op, from the dispatch to the op of the region.
//
// An adapter gives the depth of an op to dispatch_op.  The ring, the detect
// stage, the work of the build stage and the build carry it beside the scope
// hash and the callsite hash, and the build writes it into the TraceEntry of
// the op.  A stage that drops the depth gives the region an op of depth zero.
// The depths below go round several values, so a dropped or moved depth reads
// as a different depth.
//
// The record stack of the Vigil counts the open nests.  Each nest takes the
// count as its depth, and each nest that opens inside it gets the count plus
// one.

#include "vigil_rig.h"

#include <crucible/ValidatedOp.h>
#include <crucible/Vigil.h>
#include "test_assert.h"
#include "test_harness.h"

#include <cstdint>

namespace test_vigil {

namespace {

using divrig::K;
using divrig::NUM_OPS;

// The depth of op op_idx of each iteration.  Each value is the depth that a
// post-order nest gives: op 0 runs inside op 1, op 2 runs inside op 3, op 3
// runs inside op 4, and op 6 runs inside op 7.
constexpr ::fixy::FixedArray<uint16_t, NUM_OPS> DEPTH{1, 0, 2, 1, 0, 0, 1, 0};

[[nodiscard]] crucible::OpDepth depth_of(uint32_t op_idx) noexcept {
    return crucible::OpDepth{crucible::test::element_at(DEPTH, op_idx)};
}

void dispatch_with_depth(crucible::Vigil& vigil, uint32_t iter, uint32_t op_idx) {
    const divrig::OpData made = divrig::op_at(iter, op_idx);
    const crucible::DispatchResult result =
        vigil.dispatch_op(crucible::test::certify_synthetic_op(made.entry, made.metas, made.n_metas),
                          crucible::ScopeHash{}, crucible::CallsiteHash{}, depth_of(op_idx));
    assert(result.action == crucible::DispatchResult::Action::RECORD);
}

}  // namespace

void test_region_keeps_the_depth_of_each_op() {
    crucible::Vigil vigil;

    // Two whole iterations and the head of a third: the shortest input that
    // makes the detector publish a region.
    for (uint32_t iter = 0; iter < 2; ++iter) {
        for (uint32_t op = 0; op < NUM_OPS; ++op)
            dispatch_with_depth(vigil, iter, op);
    }
    for (uint32_t op = 0; op < K; ++op)
        dispatch_with_depth(vigil, 2, op);
    crucible::test::flush_and_wait_region_published(vigil);

    const ::fixy::Ref<const crucible::RegionNode> region = vigil.active_region().expect("the flush published a region");
    assert(region->num_ops() == NUM_OPS);
    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        assert(region->op_at(op).schema_hash == divrig::SCHEMA[op]);
        assert(region->op_at(op).depth == depth_of(op) && "the region holds the depth that the dispatch gave");
    }

    crucible::test::pass("  test_region_keeps_the_depth_of_each_op: PASSED\n");
}

// A period of more ops than the first capacity of the ops that the detect
// stage holds (4096), so that the detect stage grows its arrays while it
// holds the period.  The growth copies the depth of each held op.
void test_region_keeps_the_depth_past_a_growth_of_the_held_ops() {
    constexpr uint32_t kLongPeriod = 5000;
    constexpr uint16_t kDepths = 5;
    crucible::Vigil vigil;
    const auto dispatch = [&vigil](uint32_t op) {
        crucible::TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{0x10000 + op};
        entry.shape_hash = crucible::ShapeHash{0x20000 + op};
        const crucible::DispatchResult result =
            vigil.dispatch_op(crucible::test::certify_synthetic_op(entry), crucible::ScopeHash{},
                              crucible::CallsiteHash{}, crucible::OpDepth{static_cast<uint16_t>(op % kDepths)});
        assert(result.action == crucible::DispatchResult::Action::RECORD);
    };
    for (uint32_t iter = 0; iter < 2; ++iter) {
        for (uint32_t op = 0; op < kLongPeriod; ++op)
            dispatch(op);
    }
    for (uint32_t op = 0; op < K; ++op)
        dispatch(op);
    crucible::test::flush_and_wait_region_published(vigil);

    const ::fixy::Ref<const crucible::RegionNode> region = vigil.active_region().expect("the flush published a region");
    assert(region->num_ops() == kLongPeriod);
    for (uint32_t op = 0; op < kLongPeriod; ++op) {
        assert(region->op_at(op).schema_hash == crucible::SchemaHash{0x10000 + op});
        assert(region->op_at(op).depth == crucible::OpDepth{static_cast<uint16_t>(op % kDepths)}
               && "the growth of the held ops keeps the depth of each op");
    }

    crucible::test::pass("  test_region_keeps_the_depth_past_a_growth_of_the_held_ops: PASSED\n");
}

void test_op_nest_counts_the_open_records() {
    crucible::Vigil vigil;
    crucible::OpRecordStack& stack = vigil.op_record_stack(vigil.mint_producer_context());
    assert(stack.depth().is_top());
    {
        const crucible::OpNest outer{stack};
        assert(outer.depth() == crucible::OpDepth{0});
        assert(stack.depth() == crucible::OpDepth{1});
        {
            const crucible::OpNest middle{stack};
            const crucible::OpNest inner{stack};
            assert(middle.depth() == crucible::OpDepth{1});
            assert(inner.depth() == crucible::OpDepth{2});
            assert(stack.depth() == crucible::OpDepth{3});
        }
        // A nest that opens after two nests closed takes the depth that they
        // took first.
        const crucible::OpNest sibling{stack};
        assert(sibling.depth() == crucible::OpDepth{1});
    }
    assert(stack.depth().is_top() && "each close gives back the depth of its open");

    // The count stops at the largest value of its word.
    const crucible::OpDepth deepest{UINT16_MAX};
    assert(deepest.nested() == deepest);
    assert(crucible::OpDepth{}.nested() == crucible::OpDepth{1});

    crucible::test::pass("  test_op_nest_counts_the_open_records: PASSED\n");
}

}  // namespace test_vigil
