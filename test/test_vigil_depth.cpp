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
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace test_vigil {

namespace {

using divrig::K;
using divrig::NUM_OPS;

// The depth of op op_idx of each iteration.  Each value is the depth that a
// post-order nest gives: op 0 runs inside op 1, op 2 runs inside op 3, op 3
// runs inside op 4, and op 6 runs inside op 7.
constexpr ::fixy::FixedArray<uint16_t, NUM_OPS> DEPTH{1, 0, 2, 1, 0, 0, 1, 0};

[[nodiscard]] crucible::OpDepth depth_of(uint32_t op_idx) noexcept {
    return crucible::OpDepth{*DEPTH.get(op_idx).expect("a test reads an element at or past the count of its array")};
}

void dispatch_with_depth(crucible::Vigil& vigil, uint32_t iter, uint32_t op_idx) {
    const divrig::OpData made = divrig::op_at(iter, op_idx);
    const crucible::DispatchResult result =
        vigil.dispatch_op(crucible::test::certify_synthetic_op(made.entry, made.metas, made.n_metas),
                          crucible::ScopeHash{}, crucible::CallsiteHash{}, depth_of(op_idx));
    CRUCIBLE_TEST_ASSERT(kTest, result.action == crucible::DispatchResult::Action::RECORD);
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
    crucible::test::flush_and_wait_region_published(kTest, vigil);

    const crucible::DagRef<const crucible::RegionNode> region =
        vigil.active_region().expect("the flush published a region");
    CRUCIBLE_TEST_ASSERT(kTest, region->num_ops() == NUM_OPS);
    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        CRUCIBLE_TEST_ASSERT(kTest, region->op_at(op).schema_hash == divrig::SCHEMA[op]);
        CRUCIBLE_TEST_ASSERT(kTest, region->op_at(op).depth == depth_of(op)
                                        && "the region holds the depth that the dispatch gave");
    }

    ::fixy::check::pass(kTest, "  test_region_keeps_the_depth_of_each_op: PASSED\n");
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
        CRUCIBLE_TEST_ASSERT(kTest, result.action == crucible::DispatchResult::Action::RECORD);
    };
    for (uint32_t iter = 0; iter < 2; ++iter) {
        for (uint32_t op = 0; op < kLongPeriod; ++op)
            dispatch(op);
    }
    for (uint32_t op = 0; op < K; ++op)
        dispatch(op);
    crucible::test::flush_and_wait_region_published(kTest, vigil);

    const crucible::DagRef<const crucible::RegionNode> region =
        vigil.active_region().expect("the flush published a region");
    CRUCIBLE_TEST_ASSERT(kTest, region->num_ops() == kLongPeriod);
    for (uint32_t op = 0; op < kLongPeriod; ++op) {
        CRUCIBLE_TEST_ASSERT(kTest, region->op_at(op).schema_hash == crucible::SchemaHash{0x10000 + op});
        CRUCIBLE_TEST_ASSERT(kTest, region->op_at(op).depth == crucible::OpDepth{static_cast<uint16_t>(op % kDepths)}
                                        && "the growth of the held ops keeps the depth of each op");
    }

    ::fixy::check::pass(kTest, "  test_region_keeps_the_depth_past_a_growth_of_the_held_ops: PASSED\n");
}

void test_op_nest_counts_the_open_records() {
    crucible::Vigil vigil;
    crucible::OpRecordStack& stack = vigil.op_record_stack(vigil.mint_producer_context());
    CRUCIBLE_TEST_ASSERT(kTest, stack.depth().is_top());
    {
        const crucible::OpNest outer{stack};
        CRUCIBLE_TEST_ASSERT(kTest, outer.depth() == crucible::OpDepth{0});
        CRUCIBLE_TEST_ASSERT(kTest, stack.depth() == crucible::OpDepth{1});
        {
            const crucible::OpNest middle{stack};
            const crucible::OpNest inner{stack};
            CRUCIBLE_TEST_ASSERT(kTest, middle.depth() == crucible::OpDepth{1});
            CRUCIBLE_TEST_ASSERT(kTest, inner.depth() == crucible::OpDepth{2});
            CRUCIBLE_TEST_ASSERT(kTest, stack.depth() == crucible::OpDepth{3});
        }
        // A nest that opens after two nests closed takes the depth that they
        // took first.
        const crucible::OpNest sibling{stack};
        CRUCIBLE_TEST_ASSERT(kTest, sibling.depth() == crucible::OpDepth{1});
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.depth().is_top() && "each close gives back the depth of its open");

    // The count stops at the largest value of its word.
    const crucible::OpDepth deepest{UINT16_MAX};
    CRUCIBLE_TEST_ASSERT(kTest, deepest.nested() == deepest);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::OpDepth{}.nested() == crucible::OpDepth{1});

    ::fixy::check::pass(kTest, "  test_op_nest_counts_the_open_records: PASSED\n");
}

namespace {

// Gives the Vigil one op with no tensor at the depth, as an adapter gives an
// op of a nest.
void give_op_at(crucible::Vigil& vigil, uint64_t schema, crucible::OpDepth depth) {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema};
    entry.shape_hash = crucible::ShapeHash{schema + 0x1000};
    (void)vigil.dispatch_op(crucible::test::certify_synthetic_op(entry), crucible::ScopeHash{},
                            crucible::CallsiteHash{}, depth);
}

}  // namespace

// A nest gives its closing entry at its own depth only when an op inside it
// reached the Vigil and its own op did not.  The record stack counts each op
// that reaches the Vigil, so the count and the last depth show each entry.
void test_unrecorded_nest_gives_its_closing_entry() {
    crucible::Vigil vigil;
    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    const crucible::OpRecordStack& stack = vigil.op_record_stack(fg);
    const crucible::OpDepth top{0};
    const crucible::OpDepth child{1};
    const crucible::OpDepth grandchild{2};

    // The op raises after a child reached the Vigil: one closing entry at
    // the depth of the op.
    uint64_t given = stack.given();
    {
        const crucible::VigilOpNest raised{vigil, fg};
        give_op_at(vigil, 0x300, child);
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.given() == given + 2 && stack.last_given_depth() == top
                                    && "a raise after a child gives the closing entry at the depth of the op");

    // The op raises before an op inside it reached the Vigil: no entry.
    given = stack.given();
    { const crucible::VigilOpNest raised{vigil, fg}; }
    CRUCIBLE_TEST_ASSERT(kTest, stack.given() == given && "a raise with no child gives no entry");

    // A child raises after its own child reached the Vigil, and the unwind
    // passes through the parent: one entry at each depth, the child first.
    given = stack.given();
    {
        const crucible::VigilOpNest parent{vigil, fg};
        {
            const crucible::VigilOpNest raised{vigil, fg};
            give_op_at(vigil, 0x301, grandchild);
        }
        CRUCIBLE_TEST_ASSERT(kTest, stack.given() == given + 2 && stack.last_given_depth() == child
                                        && "the child gives its entry at its own depth");
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.given() == given + 3 && stack.last_given_depth() == top
                                    && "the parent gives its entry after the entry of the child");

    // The parent catches the raise of the child and reaches the Vigil: the
    // child gives its entry, and the parent gives none.
    given = stack.given();
    {
        const crucible::VigilOpNest parent{vigil, fg};
        {
            const crucible::VigilOpNest raised{vigil, fg};
            give_op_at(vigil, 0x302, grandchild);
        }
        give_op_at(vigil, 0x303, top);
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.given() == given + 3 && stack.last_given_depth() == top
                                    && "a parent that reaches the Vigil gives no closing entry");

    // An op of a larger depth that reached the Vigil before the nest opened
    // does not count for the nest.
    give_op_at(vigil, 0x304, crucible::OpDepth{3});
    given = stack.given();
    { const crucible::VigilOpNest raised{vigil, fg}; }
    CRUCIBLE_TEST_ASSERT(kTest, stack.given() == given && "an op given before the open does not count for the nest");

    // An op that reaches the Vigil after its child gives no entry.
    given = stack.given();
    {
        const crucible::VigilOpNest recorded{vigil, fg};
        give_op_at(vigil, 0x305, child);
        give_op_at(vigil, 0x306, top);
    }
    CRUCIBLE_TEST_ASSERT(kTest, stack.given() == given + 2 && "a recorded op gives no closing entry");

    ::fixy::check::pass(kTest, "  test_unrecorded_nest_gives_its_closing_entry: PASSED\n");
}

namespace {

// One iteration of a loop of NUM_OPS ops: op 3 runs op 2 inside it, and the
// other ops run at the top.  When does_raise is true, op 3 raises after op 2
// reached the Vigil, so its nest gives the closing entry in its place.
void give_iteration(crucible::Vigil& vigil, bool does_raise) {
    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        if (op == 2) continue;
        if (op != 3) {
            give_op_at(vigil, 0x400 + op, crucible::OpDepth{0});
            continue;
        }
        const crucible::VigilOpNest nest{vigil, fg};
        give_op_at(vigil, 0x402, crucible::OpDepth{1});
        if (!does_raise) give_op_at(vigil, 0x403, nest.depth());
    }
}

// The count of the iterations of each loop.  The detector of a loop of the
// control publishes a region after two iterations and a head of the third.
constexpr uint32_t kIterations = 6;

}  // namespace

// A loop whose op raises after an op inside it reached the Vigil gives the
// closing entry in each iteration.  The build refuses each region that holds
// it, so the loop stays eager.  The same loop with no raise builds a region.
void test_unrecorded_nest_keeps_the_loop_eager() {
    {
        crucible::Vigil vigil;
        for (uint32_t iter = 0; iter < kIterations; ++iter)
            give_iteration(vigil, true);
        CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest,
                             vigil.flush_complete() && "flush() returned but bg thread did not finish processing");
        CRUCIBLE_TEST_ASSERT(kTest, !vigil.has_pending_region() && vigil.active_region().is_none()
                                        && "a region that holds the closing entry of a nest must not build");
    }
    {
        crucible::Vigil vigil;
        for (uint32_t iter = 0; iter < kIterations; ++iter)
            give_iteration(vigil, false);
        crucible::test::flush_and_wait_region_published(kTest, vigil);
    }

    ::fixy::check::pass(kTest, "  test_unrecorded_nest_keeps_the_loop_eager: PASSED\n");
}

namespace {

// The most iterations that a loop gives before it replays.
constexpr uint32_t kReplayBound = 16;

// The op after the nest of op 3.  The replay diverges at the close of that
// nest and records no entry for it, so op 4 is the first op that the loop
// records after the divergence.
constexpr uint32_t kFirstOpAfterTheClose = 4;

// How op 3 of an iteration of NestedLoop ends.
enum class NestEnd : uint8_t {
    // Op 3 reaches the Vigil after op 2, as in each iteration of the rig.
    RECORDED,
    // Op 3 raises after op 2 reached the Vigil, so its nest gives the closing
    // entry in its place.
    RAISED,
};

// A loop of the ops of the rig: op 3 runs op 2 inside it, and the other ops
// run at the top.  The ops of the rig have tensors, so a region of the loop
// has a memory plan and replays.
class NestedLoop {
public:
    // Gives one iteration.  after_close runs after the close of the nest of
    // op 3 and before op 4.
    template <class AfterClose>
    void give_iteration(NestEnd end, AfterClose&& after_close) {
        const crucible::VigilFgCtx fg = vigil.mint_producer_context();
        for (uint32_t op = 0; op < NUM_OPS; ++op) {
            if (op == 2) continue;
            if (op != 3) {
                give_op(divrig::op_at(iteration_, op), crucible::OpDepth{0});
                continue;
            }
            {
                const crucible::VigilOpNest nest{vigil, fg};
                give_op(divrig::op_at(iteration_, 2), crucible::OpDepth{1});
                if (end == NestEnd::RECORDED) give_op(divrig::op_at(iteration_, 3), nest.depth());
            }
            after_close();
        }
        ++iteration_;
    }

    void give_iteration(NestEnd end) {
        give_iteration(end, [] noexcept {});
    }

    // Gives recorded iterations until the loop replays, at most kReplayBound
    // of them.  A flush after each recorded iteration makes the result
    // independent of the speed of the background thread.
    void give_iterations_until_replay() {
        for (uint32_t step = 0; step < kReplayBound && !vigil.is_compiled(); ++step) {
            give_iteration(NestEnd::RECORDED);
            if (!vigil.is_compiled()) CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
        }
    }

    crucible::Vigil vigil;

private:
    void give_op(const divrig::OpData& made, crucible::OpDepth depth) {
        (void)vigil.dispatch_op(crucible::test::certify_synthetic_op(made.entry, made.metas, made.n_metas),
                                crucible::ScopeHash{}, crucible::CallsiteHash{}, depth);
    }

    uint32_t iteration_ = 0;
};

// Gives a replayed loop one iteration in which op 3 raises.  The replay
// diverges at the closing entry of the nest, before op 4 arrives.  A replay
// that took the close with no compare would stay compiled until op 4.
void give_raise_to_a_replayed_loop(NestedLoop& loop) {
    loop.give_iterations_until_replay();
    CRUCIBLE_TEST_ASSERT(kTest, loop.vigil.is_compiled() && loop.vigil.diverged_count() == 0
                                    && "the loop replays before the raise");
    // The replay of the next iteration starts at op 0 of the region.
    loop.give_iteration(NestEnd::RECORDED);
    CRUCIBLE_TEST_ASSERT(kTest, loop.vigil.is_compiled() && loop.vigil.diverged_count() == 0);

    bool is_close_checked = false;
    loop.give_iteration(NestEnd::RAISED, [&loop, &is_close_checked] {
        CRUCIBLE_TEST_ASSERT(kTest, !loop.vigil.is_compiled() && loop.vigil.diverged_count() == 1
                                        && "the replay diverges at the close of the nest");
        is_close_checked = true;
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_close_checked);
}

}  // namespace

// The raise of one iteration does not change the loop.  So after the
// divergence at the close, the loop replays again with a region of the ops of
// one iteration.  The region starts at the first op that the loop recorded
// after the close, and it holds no closing entry.
void test_replay_diverges_at_the_close_of_a_nest() {
    NestedLoop loop;
    give_raise_to_a_replayed_loop(loop);

    loop.give_iterations_until_replay();
    CRUCIBLE_TEST_ASSERT(kTest, loop.vigil.is_compiled() && "the loop replays again after the divergence");
    const crucible::DagRef<const crucible::RegionNode> region = loop.vigil.active_region().expect("a region replays");
    CRUCIBLE_TEST_ASSERT(kTest, region->num_ops() == NUM_OPS && "the region holds the ops of one iteration");
    for (uint32_t op = 0; op < NUM_OPS; ++op) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             region->op_at(op).schema_hash == divrig::SCHEMA[(kFirstOpAfterTheClose + op) % NUM_OPS]
                                 && "the region starts at the first op that the loop recorded after the close");
    }

    constexpr uint32_t kReplayed = 3;
    loop.give_iteration(NestEnd::RECORDED);
    const uint64_t replayed = loop.vigil.compiled_iterations();
    for (uint32_t step = 0; step < kReplayed; ++step)
        loop.give_iteration(NestEnd::RECORDED);
    CRUCIBLE_TEST_ASSERT(kTest,
                         loop.vigil.diverged_count() == 1 && loop.vigil.compiled_iterations() == replayed + kReplayed);

    ::fixy::check::pass(kTest, "  test_replay_diverges_at_the_close_of_a_nest: PASSED\n");
}

// A loop whose op 3 raises in each iteration after the divergence gives the
// closing entry in each iteration.  The build refuses each region that holds
// it, so the loop stays eager, and it diverges no more.
void test_raise_in_each_iteration_after_a_replay_keeps_the_loop_eager() {
    NestedLoop loop;
    give_raise_to_a_replayed_loop(loop);

    for (uint32_t step = 0; step < kReplayBound; ++step) {
        loop.give_iteration(NestEnd::RAISED);
        CRUCIBLE_TEST_ASSERT(kTest, loop.vigil.flush(kTest).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, !loop.vigil.is_compiled() && "a loop that raises in each iteration stays eager");
    }
    CRUCIBLE_TEST_ASSERT(kTest, loop.vigil.diverged_count() == 1);

    ::fixy::check::pass(kTest, "  test_raise_in_each_iteration_after_a_replay_keeps_the_loop_eager: PASSED\n");
}

}  // namespace test_vigil
