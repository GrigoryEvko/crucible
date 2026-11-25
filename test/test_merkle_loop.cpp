// The LoopNode of the Merkle DAG: its body, its merkle hash, the regions
// that a walk collects from it, and its replay.

#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::ContentHash;
using crucible::MerkleHash;
using crucible::SchemaHash;

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 16});

    auto body_ops1_run = arena.array(test.alloc, 2, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry> const body_ops1 = body_ops1_run.view();
    body_ops1.get(0).expect("the run holds two ops")->schema_hash = SchemaHash{0x1111};
    body_ops1.get(1).expect("the run holds two ops")->schema_hash = SchemaHash{0x2222};
    // The nodes are held as references: a Ref local in this large function
    // costs the Debug compile far more than a reference.
    crucible::RegionNode& body_r1 = *crucible::make_region(test.alloc, arena, body_ops1_run);

    auto body_ops2_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry> const body_ops2 = body_ops2_run.view();
    body_ops2.get(0).expect("the run holds one op")->schema_hash = SchemaHash{0x3333};
    crucible::RegionNode& body_r2 = *crucible::make_region(test.alloc, arena, body_ops2_run);

    crucible::TraceNode& body_term = *crucible::make_terminal(test.alloc, arena);
    body_r1.next = crucible::make_link(body_r2);
    body_r2.next = crucible::make_link(body_term);

    ContentHash body_ch = crucible::compute_body_content_hash(body_r1);
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(body_ch));

    CRUCIBLE_TEST_ASSERT(kTest, crucible::compute_body_content_hash(body_r1) == body_ch);

    auto fb_edges_run = arena.array(test.alloc, 1, crucible::FeedbackEdge{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::FeedbackEdge> const fb_edges = fb_edges_run.view();
    fb_edges.get(0).expect("the run holds one edge")->output_idx = 0;
    fb_edges.get(0).expect("the run holds one edge")->input_idx = 0;

    crucible::LoopNode& loop =
        *crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges_run, crucible::LoopTermKind::REPEAT, 4);

    CRUCIBLE_TEST_ASSERT(kTest, loop.kind == crucible::TraceNodeKind::LOOP);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(loop.body.option(), body_r1));
    CRUCIBLE_TEST_ASSERT(kTest, loop.body_content_hash == body_ch);
    CRUCIBLE_TEST_ASSERT(kTest, loop.num_feedback() == 1);
    CRUCIBLE_TEST_ASSERT(kTest,
                         loop.feedback_edges.view().get(0).expect("the loop holds one feedback edge")->output_idx == 0);
    CRUCIBLE_TEST_ASSERT(kTest, loop.term_kind == crucible::LoopTermKind::REPEAT);
    CRUCIBLE_TEST_ASSERT(kTest, loop.repeat_count == 4);
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::bit_cast<uint32_t>(loop.epsilon) == 0);
    static_assert(sizeof(crucible::LoopNode) == 72);
    static_assert(sizeof(crucible::FeedbackEdge) == 4);

    auto final_ops_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry> const final_ops = final_ops_run.view();
    final_ops.get(0).expect("the run holds one op")->schema_hash = SchemaHash{0x4444};
    crucible::RegionNode& final_region = *crucible::make_region(test.alloc, arena, final_ops_run);
    crucible::TraceNode& dag_term = *crucible::make_terminal(test.alloc, arena);
    final_region.next = crucible::make_link(dag_term);
    loop.next = crucible::make_link(final_region);

    // Merkle hash covers loop body, feedback, termination, and continuation
    crucible::recompute_merkle(loop);
    MerkleHash loop_merkle = loop.merkle_hash;
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(loop_merkle));

    crucible::LoopNode& loop2 =
        *crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges_run, crucible::LoopTermKind::REPEAT, 8);
    loop2.next = crucible::make_link(final_region);
    crucible::recompute_merkle(loop2);
    CRUCIBLE_TEST_ASSERT(kTest, loop2.merkle_hash != loop_merkle);

    crucible::LoopNode& loop3 = *crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges_run,
                                                     crucible::LoopTermKind::UNTIL, 4, 0.001f);
    loop3.next = crucible::make_link(final_region);
    crucible::recompute_merkle(loop3);
    CRUCIBLE_TEST_ASSERT(kTest, loop3.merkle_hash != loop_merkle);
    CRUCIBLE_TEST_ASSERT(kTest, loop3.merkle_hash != loop2.merkle_hash);

    auto fb_edges2_run = arena.array(test.alloc, 2, crucible::FeedbackEdge{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::FeedbackEdge> const fb_edges2 = fb_edges2_run.view();
    *fb_edges2.get(0).expect("the run holds two edges") = {.output_idx = 0, .input_idx = 0};
    *fb_edges2.get(1).expect("the run holds two edges") = {.output_idx = 1, .input_idx = 1};
    crucible::LoopNode& loop4 =
        *crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges2_run, crucible::LoopTermKind::REPEAT, 4);
    loop4.next = crucible::make_link(final_region);
    crucible::recompute_merkle(loop4);
    CRUCIBLE_TEST_ASSERT(kTest, loop4.merkle_hash != loop_merkle);

    CRUCIBLE_TEST_ASSERT(kTest, crucible::feedback_signature({}) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::feedback_signature(loop.feedback_edges.view()) != 0);

    auto const& loop_read = loop;
    auto const [n_collected, collected] = ::foundation::core::build_fixed_array<crucible::RegionNode const*, 16>(
        [&loop_read](auto run) noexcept { return crucible::collect_regions(loop_read, run); });
    // Two body regions, counted once, plus the region after the loop.
    CRUCIBLE_TEST_ASSERT(kTest, n_collected == 3);
    CRUCIBLE_TEST_ASSERT(kTest, *collected.view().get(0).expect("the run holds 16 regions") == &body_r1);
    CRUCIBLE_TEST_ASSERT(kTest, *collected.view().get(1).expect("the run holds 16 regions") == &body_r2);
    CRUCIBLE_TEST_ASSERT(kTest, *collected.view().get(2).expect("the run holds 16 regions") == &final_region);

    // The replays of this test share one guard callback.  The replays that
    // count regions share one closure type of counter_of, so the unit
    // compiles two instantiations of replay and not one for each call.
    auto never_diverges = [](const crucible::Guard&) -> int64_t { return 0; };
    auto counter_of = [](uint32_t& count) { return [&count](crucible::RegionNode const&) { ++count; }; };

    uint32_t exec_count = 0;
    ::fixy::FixedList<ContentHash, 16> replay_log{};
    bool replay_ok = crucible::replay(loop, never_diverges, [&](crucible::RegionNode const& r) {
        static_cast<void>(replay_log.append(r.content_hash).expect("the replay runs at most 16 regions"));
        exec_count++;
    });
    CRUCIBLE_TEST_ASSERT(kTest, replay_ok);
    // 4 iterations × 2 body regions + 1 continuation region.
    CRUCIBLE_TEST_ASSERT(kTest, exec_count == 9);
    CRUCIBLE_TEST_ASSERT(kTest, replay_log.size() == 9);
    auto const logged = [&replay_log](uint32_t position) noexcept {
        return *replay_log.get(position).expect("the log holds 9 regions");
    };
    for (uint32_t i = 0; i < 8; i += 2) {
        CRUCIBLE_TEST_ASSERT(kTest, logged(i) == body_r1.content_hash);
        CRUCIBLE_TEST_ASSERT(kTest, logged(i + 1) == body_r2.content_hash);
    }
    CRUCIBLE_TEST_ASSERT(kTest, logged(8) == final_region.content_hash);

    crucible::LoopNode& loop_zero =
        *crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges_run, crucible::LoopTermKind::REPEAT, 0);
    loop_zero.next = crucible::make_link(final_region);
    uint32_t zero_count = 0;
    bool zero_ok = crucible::replay(loop_zero, never_diverges, counter_of(zero_count));
    CRUCIBLE_TEST_ASSERT(kTest, zero_ok);
    CRUCIBLE_TEST_ASSERT(kTest, zero_count == 1);  // the continuation region alone

    auto alt_body_ops_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry> const alt_body_ops = alt_body_ops_run.view();
    alt_body_ops.get(0).expect("the run holds one op")->schema_hash = SchemaHash{0x9999};
    crucible::RegionNode& alt_body = *crucible::make_region(test.alloc, arena, alt_body_ops_run);
    crucible::TraceNode& alt_body_term = *crucible::make_terminal(test.alloc, arena);
    alt_body.next = crucible::make_link(alt_body_term);
    ContentHash alt_ch = crucible::compute_body_content_hash(alt_body);
    CRUCIBLE_TEST_ASSERT(kTest, alt_ch != body_ch);

    // ── LoopNode: epsilon is identity, repeat_count is execution ──────
    //
    // replay walks repeat_count and never reads epsilon, for both
    // termination kinds.  That is the DetSafe axiom rather than an
    // oversight, and it is worth a test in both directions: a change that
    // made replay converge on epsilon would break determinism, and a change
    // that dropped epsilon from the hash would let two loops with different
    // thresholds share a compiled kernel.
    {
        auto loop_body_ops_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<crucible::TraceEntry> const loop_body_ops = loop_body_ops_run.view();
        loop_body_ops.get(0).expect("the run holds one op")->schema_hash = SchemaHash{0x5EED};

        // Two loops that agree on everything but the threshold.  Each gets
        // its own body chain: recompute_merkle writes into the nodes it
        // walks, so a shared body would have one loop's pass overwrite the
        // other's.
        auto build_until_loop = [&](float epsilon) {
            const auto body = crucible::make_region(test.alloc, arena, loop_body_ops_run);
            body->next = crucible::make_link(*crucible::make_terminal(test.alloc, arena));
            const auto node = crucible::make_loop(test.alloc, arena, *body, crucible::compute_body_content_hash(*body),
                                                  ::fixy::ArenaRun<crucible::FeedbackEdge const, crucible::DagArena>{},
                                                  crucible::LoopTermKind::UNTIL, 3, epsilon);
            node->next = crucible::make_link(*crucible::make_terminal(test.alloc, arena));
            crucible::recompute_merkle(*node);
            return node;
        };

        crucible::LoopNode& loose = *build_until_loop(0.5f);
        crucible::LoopNode& tight = *build_until_loop(0.001f);

        uint32_t loose_regions = 0;
        CRUCIBLE_TEST_ASSERT(kTest, crucible::replay(loose, never_diverges, counter_of(loose_regions)));
        uint32_t tight_regions = 0;
        CRUCIBLE_TEST_ASSERT(kTest, crucible::replay(tight, never_diverges, counter_of(tight_regions)));

        // 500x the threshold and the same number of iterations.  A replay
        // that consulted epsilon could not produce this.
        CRUCIBLE_TEST_ASSERT(kTest, loose_regions == 3 && "an UNTIL loop must replay the iteration count it observed");
        CRUCIBLE_TEST_ASSERT(kTest, tight_regions == 3);
        CRUCIBLE_TEST_ASSERT(kTest, loose_regions == tight_regions
                                        && "replay read epsilon: the same recorded trace now executes a "
                                           "number of times that depends on the threshold, so two replays of "
                                           "one trace can disagree");

        // The other direction.  Identical ops, identical count, different
        // numerics — different computation, so different cache key.
        CRUCIBLE_TEST_ASSERT(kTest, loose.merkle_hash != tight.merkle_hash
                                        && "epsilon left the termination hash: two loops with different "
                                           "convergence thresholds now share a merkle hash, so one's "
                                           "compiled kernel can serve the other");

        ::fixy::report(::fixy::Sink::Out,
                       "  loop replay walks repeat_count and ignores epsilon; epsilon stays in the hash\n");
    }

    ::fixy::check::pass(kTest, "test_merkle_loop: all tests passed\n");
    return 0;
}
