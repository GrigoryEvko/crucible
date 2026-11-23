#include <crucible/cntp/PathSwap.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/session/Entry.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

// commit_sender carries a [[deprecated("CRUCIBLE_STUB:...")]] attribute,
// because it migrates no in-flight data.  This file calls it on purpose to pin
// that loss, so the warning is suppressed for this file alone.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cntp = crucible::cntp;
namespace fe = ::foundation::effects;
namespace sess = ::fixy::session;

namespace {

struct Wire {
    int id = 0;
    int last = 0;
};

using Proto = sess::Send<int, sess::End>;

// A trying write: it always has room.
bool send_int(Wire& wire, int& value) noexcept {
    wire.last = value;
    return true;
}

// A count of nanoseconds on the clock of the swapper.
using Nanos = cntp::SwapNanoseconds;

// The three ids of a plan.  A caller names each field, so two ids cannot
// swap.
struct PlanIds {
    std::uint64_t flow = 0;
    std::uint64_t old_path = 0;
    std::uint64_t new_path = 0;
};

cntp::DeclaredPathSwapPlan make_plan_for(PlanIds ids) {
    auto flow = cntp::admit_flow_id(cntp::FlowId{ids.flow}).expect("the flow id is positive");
    auto old_path = cntp::admit_path_id(cntp::PathId{ids.old_path}).expect("the old path id is positive");
    auto new_path = cntp::admit_path_id(cntp::PathId{ids.new_path}).expect("the new path id is positive");
    auto timeout = cntp::admit_swap_timeout(Nanos{1000}).expect("1000 ns is positive");
    return cntp::mint_path_swap_plan(flow, cntp::OldPathId{old_path}, cntp::NewPathId{new_path}, timeout)
        .expect("the two paths differ");
}

cntp::DeclaredPathSwapPlan make_plan() { return make_plan_for({.flow = 10, .old_path = 20, .new_path = 30}); }

// The second plan of the race tests.  Each field differs from the first
// plan, so a read that mixes the two plans names paths that no plan named.
cntp::DeclaredPathSwapPlan make_other_plan() { return make_plan_for({.flow = 11, .old_path = 40, .new_path = 50}); }

[[nodiscard]] bool names_one_declared_plan(std::uint64_t flow_id, std::uint64_t old_path_id,
                                           std::uint64_t new_path_id) noexcept {
    return (flow_id == 10 && old_path_id == 20 && new_path_id == 30)
        || (flow_id == 11 && old_path_id == 40 && new_path_id == 50);
}

// A count that each thread of a race adds to.
using RaceCount = ::fixy::Atomic<std::uint32_t, ::fixy::Counter>;

// Runs the bodies of one race, each on a thread of its own, and joins each
// thread.  The bodies share the swapper on purpose, so the race goes through
// the raw probe of the thread door.  The door starts the threads in the order
// of the bodies.
template <typename... Bodies>
void run_race(::fixy::BgLoadCtx const& bg, Bodies&&... bodies) noexcept {
    ::fixy::thread::run_joined_raw_probe(bg, kTest, "the threads of a race of the swapper share the swapper on purpose",
                                         ::fixy::thread::ThreadOptions{}, static_cast<Bodies&&>(bodies)...)
        .expect("the thread door starts each thread of the race");
}

// Spins until the gate of a race opens.
void wait_for_start(::fixy::Atomic<bool> const& start) noexcept {
    while (!start.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

void test_admission() {
    static_assert(cntp::swap_state_name(cntp::SwapState::Draining) == "Draining");
    static_assert(cntp::swap_error_name(cntp::SwapError::SamePath) == "SamePath");
    CRUCIBLE_TEST_ASSERT(kTest, cntp::swap_state_name(static_cast<cntp::SwapState>(200)) == "<unknown SwapState>");

    auto zero_flow = cntp::admit_flow_id(cntp::FlowId{0});
    CRUCIBLE_TEST_ASSERT(kTest, zero_flow.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_flow.err() == ::fixy::some(cntp::SwapError::InvalidFlowId));

    auto zero_path = cntp::admit_path_id(cntp::PathId{0});
    CRUCIBLE_TEST_ASSERT(kTest, zero_path.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_path.err() == ::fixy::some(cntp::SwapError::InvalidPathId));

    auto zero_timeout = cntp::admit_swap_timeout(Nanos{0});
    CRUCIBLE_TEST_ASSERT(kTest, zero_timeout.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_timeout.err() == ::fixy::some(cntp::SwapError::Timeout));

    auto flow = cntp::admit_flow_id(cntp::FlowId{1}).expect("1 is positive");
    auto path = cntp::admit_path_id(cntp::PathId{1}).expect("1 is positive");
    auto timeout = cntp::admit_swap_timeout(Nanos{10}).expect("10 ns is positive");
    CRUCIBLE_TEST_ASSERT(kTest,
                         cntp::mint_path_swap_plan(flow, cntp::OldPathId{path}, cntp::NewPathId{path}, timeout).err()
                             == ::fixy::some(cntp::SwapError::SamePath));

    auto plan = make_plan();
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().flow_id().value() == cntp::FlowId{10});
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().old_path().value() == cntp::PathId{20});
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().new_path().value() == cntp::PathId{30});
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().timeout().value() == Nanos{1000});

    // The step check reads the state that a step leaves and the state that
    // it enters in their roles: Stable leads to Draining, and not back.
    static_assert(cntp::is_valid_path_swap_transition(cntp::SwapFromState{cntp::SwapState::Stable},
                                                      cntp::SwapToState{cntp::SwapState::Draining}));
    static_assert(!cntp::is_valid_path_swap_transition(cntp::SwapFromState{cntp::SwapState::Draining},
                                                       cntp::SwapToState{cntp::SwapState::Stable}));

    ::fixy::check::pass(kTest, "  test_admission: PASSED\n");
}

void test_state_machine_and_session_resource_transition() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<8>(init);
    static_assert(std::is_same_v<decltype(swapper), cntp::PathSwapper<8>>);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::Stable);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.plan(bg).is_none());

    auto plan = make_plan();
    CRUCIBLE_TEST_ASSERT(kTest, swapper.begin_swap(bg, plan, Nanos{100}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::Draining);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.deadline(bg) == Nanos{1100});
    CRUCIBLE_TEST_ASSERT(kTest, swapper.plan(bg).is_some());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.plan(bg).expect("the swap has a plan").old_path().value() == cntp::PathId{20});

    CRUCIBLE_TEST_ASSERT(kTest, swapper.receiver_accepts_bidir(bg, Nanos{200}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::BidirReceive);

    CRUCIBLE_TEST_ASSERT(kTest, swapper.sender_observed_drain_ack(bg, Nanos{300}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::NewPathFlushing);

    auto old_handle = sess::mint_session<Proto>(bg, Wire{.id = 1});
    auto swapped = swapper.commit_sender(bg, std::move(old_handle), Wire{.id = 2}, Nanos{400})
                       .expect("the swap is at NewPathFlushing");
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::Complete);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_count(bg) == 4);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.sequence(bg) == 4);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_at(bg, 3).to == cntp::SwapState::Complete);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_at(bg, 0).flow_id == 10);
    CRUCIBLE_TEST_ASSERT(kTest, swapped.resource().id == 2);

    auto end = std::move(swapped).send(42, send_int);
    auto final_wire = std::move(end).close();
    CRUCIBLE_TEST_ASSERT(kTest, final_wire.id == 2);
    CRUCIBLE_TEST_ASSERT(kTest, final_wire.last == 42);

    ::fixy::check::pass(kTest, "  test_state_machine_and_session_resource_transition: PASSED\n");
}

// The swapper is address-stable and therefore shareable across threads,
// which means its state is read by an observer while a background
// thread is writing it.  A plain enum field would be torn by that, and
// the observer would see a value that is not any of the states.
void test_concurrent_observer_sees_only_valid_states() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<16>(init);

    ::fixy::Atomic<bool> writer_done{false};
    RaceCount torn_observations{0};
    RaceCount total_observations{0};
    auto observer = [&swapper, &writer_done, &torn_observations, &total_observations] noexcept {
        // The loop runs its body before testing the flag, so at least
        // one observation happens even when the writer finishes first.
        // The claim under test is that no read is torn, not that the
        // observer wins the start-up race.
        do {
            const auto observed = swapper.state();
            static_cast<void>(total_observations.fetch_add_relaxed(1));
            switch (observed) {
                case cntp::SwapState::Stable:
                case cntp::SwapState::Draining:
                case cntp::SwapState::BidirReceive:
                case cntp::SwapState::NewPathFlushing:
                case cntp::SwapState::Complete:
                case cntp::SwapState::Failed:
                    break;
                default:
                    static_cast<void>(torn_observations.fetch_add_relaxed(1));
            }
        } while (!writer_done.load_acquire());
    };

    auto plan = make_plan();
    auto writer = [&swapper, &plan, &writer_done](::fixy::BgLoadCtx const& ctx) noexcept {
        for (std::uint32_t cycle = 0; cycle < 2000; ++cycle) {
            const std::uint64_t base = static_cast<std::uint64_t>(cycle) * 100;
            CRUCIBLE_TEST_ASSERT(kTest, swapper.begin_swap(ctx, plan, Nanos{base + 1}).is_ok());
            CRUCIBLE_TEST_ASSERT(kTest, swapper.receiver_accepts_bidir(ctx, Nanos{base + 2}).is_ok());
            CRUCIBLE_TEST_ASSERT(kTest, swapper.sender_observed_drain_ack(ctx, Nanos{base + 3}).is_ok());
            CRUCIBLE_TEST_ASSERT(kTest, swapper.complete_receiver(ctx, Nanos{base + 4}).is_ok());
        }
        writer_done.store_release(true);
    };
    run_race(bg, std::move(observer), std::move(writer));
    CRUCIBLE_TEST_ASSERT(kTest, torn_observations.load_acquire() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, total_observations.load_acquire() > 0);

    ::fixy::check::pass(kTest, "  test_concurrent_observer_sees_only_valid_states: PASSED\n");
}

// Committing a swap moves the state machine and nothing else.  Data
// still buffered on the old resource is dropped, where a transport that
// could genuinely migrate a path would replay or hand it over.  The
// sentinel value planted below stands in for such buffered data, and
// the old resource is consumed by the commit, so its absence from the
// new one is the whole proof: there is no path by which it could
// arrive.
void test_commit_sender_loses_in_flight_bytes() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<8>(init);
    auto plan = make_plan();
    CRUCIBLE_TEST_ASSERT(kTest, swapper.begin_swap(bg, plan, Nanos{100}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.receiver_accepts_bidir(bg, Nanos{200}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.sender_observed_drain_ack(bg, Nanos{300}).is_ok());

    Wire old_wire{.id = 7, .last = 0xBEEF};  // stands in for buffered data
    auto old_handle = sess::mint_session<Proto>(bg, std::move(old_wire));

    Wire new_wire{.id = 9, .last = 0};
    auto swapped = swapper.commit_sender(bg, std::move(old_handle), std::move(new_wire), Nanos{400})
                       .expect("the swap is at NewPathFlushing");
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::Complete);

    // A migration-capable backend would flip the marker and carry the
    // sentinel across, at which point these two lines become a positive
    // check instead of a negative one.
    CRUCIBLE_TEST_ASSERT(kTest, swapped.resource().id == 9);
    CRUCIBLE_TEST_ASSERT(kTest, swapped.resource().last == 0);
    CRUCIBLE_TEST_ASSERT(kTest, swapped.resource().last != 0xBEEF);

    // A session handle must be run to its end.  Dropping one aborts, so
    // the send and close here are the lifetime contract and not extra
    // coverage.
    auto end = std::move(swapped).send(7, send_int);
    auto final_wire = std::move(end).close();
    CRUCIBLE_TEST_ASSERT(kTest, final_wire.id == 9);
    CRUCIBLE_TEST_ASSERT(kTest, final_wire.last == 7);

    ::fixy::check::pass(kTest, "  test_commit_sender_loses_in_flight_bytes: PASSED\n");
}

// A transition that read the state and then stored it without a gate
// would let two threads both see the same source state and both append
// an event, so one edge would appear twice in the audit log.  The gate
// of the swapper runs one transition at a time, so exactly one thread
// wins the edge.  The losers are then refused, because the state they
// would transition from is no longer the state that is there.
void test_concurrent_complete_receiver_exactly_one_wins() {
    constexpr int kRacers = 16;
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<32>(init);
    auto plan = make_plan();

    // The three setup transitions run on this thread alone, so the
    // event count is known exactly before the race begins and the one
    // event the race is allowed to add can be counted afterwards.
    CRUCIBLE_TEST_ASSERT(kTest, swapper.begin_swap(bg, plan, Nanos{100}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.receiver_accepts_bidir(bg, Nanos{200}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.sender_observed_drain_ack(bg, Nanos{300}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_count(bg) == 3);

    RaceCount winners{0};
    RaceCount losers{0};
    RaceCount next_racer{0};
    ::fixy::Atomic<bool> start{false};

    auto racer = [&swapper, &winners, &losers, &next_racer, &start](::fixy::BgLoadCtx const& ctx) noexcept {
        const std::uint32_t idx = next_racer.fetch_add_relaxed(1);
        // The threads are held at this gate so that they enter the
        // exchange having seen the same state.  Without the gate the
        // first thread to wake usually finishes before the others
        // start and the race never happens.
        wait_for_start(start);
        Nanos const now{400 + static_cast<std::uint64_t>(idx)};
        auto result = swapper.complete_receiver(ctx, now);
        if (result.is_ok()) {
            static_cast<void>(winners.fetch_add_relaxed(1));
        } else {
            CRUCIBLE_TEST_ASSERT(kTest, result.err() == ::fixy::some(cntp::SwapError::InvalidTransition));
            static_cast<void>(losers.fetch_add_relaxed(1));
        }
    };
    // The opener starts after each racer, and it opens the gate at once.
    auto opener = [&start] noexcept { start.store_release(true); };

    // Each racer is a copy of one body, so each thread runs its own object.
    run_race(bg, auto{racer}, auto{racer}, auto{racer}, auto{racer}, auto{racer}, auto{racer}, auto{racer}, auto{racer},
             auto{racer}, auto{racer}, auto{racer}, auto{racer}, auto{racer}, auto{racer}, auto{racer},
             std::move(racer), std::move(opener));

    // Each racer of the list above took one index, so the list holds kRacers
    // racers.
    CRUCIBLE_TEST_ASSERT(kTest, next_racer.load_acquire() == kRacers);
    CRUCIBLE_TEST_ASSERT(kTest, winners.load_acquire() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, losers.load_acquire() == kRacers - 1);

    // One winner is not enough on its own: the log must have gained one
    // event and not one per thread that observed the source state.
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_count(bg) == 4);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_at(bg, 3).from == cntp::SwapState::NewPathFlushing);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_at(bg, 3).to == cntp::SwapState::Complete);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::Complete);

    ::fixy::check::pass(kTest, "  test_concurrent_complete_receiver_exactly_one_wins: PASSED\n");
}

// A reader of the plan and of the audit log runs beside a writer that
// swaps between two plans.  Each read must give one whole plan and one
// whole event.  Under ThreadSanitizer a read that the swapper does not
// order against the writer is a reported race.
void test_plan_reader_races_the_writer() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<16>(init);
    const auto first = make_plan();
    const auto second = make_other_plan();

    ::fixy::Atomic<bool> writer_done{false};
    RaceCount mixed_reads{0};
    auto reader = [&swapper, &writer_done, &mixed_reads](::fixy::BgLoadCtx const& ctx) noexcept {
        do {
            auto const seen = swapper.plan(ctx);
            for (cntp::PathSwapPlan const& plan : seen) {
                if (!names_one_declared_plan(plan.flow_id().value().raw(), plan.old_path().value().raw(),
                                             plan.new_path().value().raw())) {
                    static_cast<void>(mixed_reads.fetch_add_relaxed(1));
                }
            }
            if (swapper.event_count(ctx) > 0) {
                // The latest event.  The ring holds each event at its
                // sequence number minus one, modulo the ring size.
                const auto event = swapper.event_at(ctx, swapper.sequence(ctx) - 1);
                if (!names_one_declared_plan(event.flow_id, event.old_path, event.new_path)
                    || !cntp::is_valid_path_swap_transition(cntp::SwapFromState{event.from},
                                                            cntp::SwapToState{event.to})) {
                    static_cast<void>(mixed_reads.fetch_add_relaxed(1));
                }
            }
        } while (!writer_done.load_acquire());
    };

    auto writer = [&swapper, &first, &second, &writer_done](::fixy::BgLoadCtx const& ctx) noexcept {
        for (std::uint32_t cycle = 0; cycle < 2000; ++cycle) {
            const std::uint64_t base = static_cast<std::uint64_t>(cycle) * 100;
            const auto& plan = (cycle % 2u == 0u) ? first : second;
            CRUCIBLE_TEST_ASSERT(kTest, swapper.begin_swap(ctx, plan, Nanos{base + 1}).is_ok());
            CRUCIBLE_TEST_ASSERT(kTest, swapper.receiver_accepts_bidir(ctx, Nanos{base + 2}).is_ok());
            CRUCIBLE_TEST_ASSERT(kTest, swapper.sender_observed_drain_ack(ctx, Nanos{base + 3}).is_ok());
            CRUCIBLE_TEST_ASSERT(kTest, swapper.complete_receiver(ctx, Nanos{base + 4}).is_ok());
        }
        writer_done.store_release(true);
    };
    run_race(bg, std::move(reader), std::move(writer));
    CRUCIBLE_TEST_ASSERT(kTest, mixed_reads.load_acquire() == 0);

    ::fixy::check::pass(kTest, "  test_plan_reader_races_the_writer: PASSED\n");
}

// Two threads start a swap at the same time with different plans.  One of
// them wins.  The plan that the swapper holds afterwards must be the plan
// of the winner, which is the plan that the first audit event names.  A
// loser that writes its plan after the winner has won replaces the plan of
// a swap that is already in flight.
void test_concurrent_begin_swap_keeps_the_winning_plan() {
    constexpr int kRounds = 200;
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    const auto first = make_plan();
    const auto second = make_other_plan();

    for (int round = 0; round < kRounds; ++round) {
        auto swapper = cntp::mint_path_swapper<8>(init);
        ::fixy::Atomic<bool> start{false};
        RaceCount winners{0};
        auto left = [&swapper, &start, &winners, &first](::fixy::BgLoadCtx const& ctx) noexcept {
            wait_for_start(start);
            if (swapper.begin_swap(ctx, first, Nanos{100}).is_ok()) {
                static_cast<void>(winners.fetch_add_relaxed(1));
            }
        };
        auto right = [&swapper, &start, &winners, &second](::fixy::BgLoadCtx const& ctx) noexcept {
            wait_for_start(start);
            if (swapper.begin_swap(ctx, second, Nanos{100}).is_ok()) {
                static_cast<void>(winners.fetch_add_relaxed(1));
            }
        };
        auto opener = [&start] noexcept { start.store_release(true); };
        run_race(bg, std::move(left), std::move(right), std::move(opener));
        CRUCIBLE_TEST_ASSERT(kTest, winners.load_acquire() == 1);
        cntp::PathSwapPlan const held = swapper.plan(bg).expect("one racer won the swap");
        CRUCIBLE_TEST_ASSERT(kTest, swapper.event_count(bg) == 1);
        const auto event = swapper.event_at(bg, 0);
        CRUCIBLE_TEST_ASSERT(kTest, event.flow_id == held.flow_id().value().raw());
        CRUCIBLE_TEST_ASSERT(kTest, event.old_path == held.old_path().value().raw());
        CRUCIBLE_TEST_ASSERT(kTest, event.new_path == held.new_path().value().raw());
    }

    ::fixy::check::pass(kTest, "  test_concurrent_begin_swap_keeps_the_winning_plan: PASSED\n");
}

void test_invalid_transition_and_timeout() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<4>(init);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.sender_observed_drain_ack(bg, Nanos{1}).err()
                                    == ::fixy::some(cntp::SwapError::InvalidTransition));
    CRUCIBLE_TEST_ASSERT(kTest, swapper.event_count(bg) == 0);

    auto plan = make_plan();
    CRUCIBLE_TEST_ASSERT(kTest, swapper.begin_swap(bg, plan, Nanos{10}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, swapper.begin_swap(bg, plan, Nanos{11}).err()
                                    == ::fixy::some(cntp::SwapError::InvalidTransition));

    CRUCIBLE_TEST_ASSERT(kTest, swapper.receiver_accepts_bidir(bg, Nanos{2000}).err()
                                    == ::fixy::some(cntp::SwapError::Timeout));
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::Failed);

    ::fixy::check::pass(kTest, "  test_invalid_transition_and_timeout: PASSED\n");
}

void test_deadline_overflow() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<4>(init);
    auto plan = make_plan();
    auto overflow = swapper.begin_swap(bg, plan, Nanos{::fixy::max_of<std::uint64_t> - 10});
    CRUCIBLE_TEST_ASSERT(kTest, overflow.err() == ::fixy::some(cntp::SwapError::DeadlineOverflow));
    CRUCIBLE_TEST_ASSERT(kTest, swapper.state() == cntp::SwapState::Stable);
    CRUCIBLE_TEST_ASSERT(kTest, swapper.plan(bg).is_none());

    ::fixy::check::pass(kTest, "  test_deadline_overflow: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositivePathId) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredPathSwapPlan) == sizeof(cntp::PathSwapPlan));
    static_assert(cntp::CtxFitsPathSwapMint<::fixy::ColdInitCtx>);
    static_assert(!cntp::CtxFitsPathSwapMint<::fixy::BgDrainCtx>);
    static_assert(cntp::CtxFitsPathSwapTransition<::fixy::BgLoadCtx>);
    static_assert(!cntp::CtxFitsPathSwapTransition<::fixy::BgDrainCtx>);
    static_assert(cntp::CtxFitsPathSwapRead<::fixy::TestRunnerCtx>);
    static_assert(!cntp::CtxFitsPathSwapTransition<::fixy::HotFgCtx>);
    static_assert(cntp::PathSwapSessionResource<Wire>);
    static_assert(!cntp::PathSwapSessionResource<Wire&>);
    static_assert(!std::is_default_constructible_v<cntp::PathSwapPlan>);
    static_assert(!std::is_default_constructible_v<cntp::DeclaredPathSwapPlan>);
    static_assert(!std::is_default_constructible_v<cntp::PathSwapper<8>>);

    // The marker is readable at every arity, so a caller can branch on
    // the gap at compile time rather than discovering it at runtime.
    static_assert(!cntp::PathSwapper<>::data_migration_implemented, "PathSwapper must advertise state-only semantics "
                                                                    "until a per-transport migration engine ships");
    static_assert(!cntp::PathSwapper<8>::data_migration_implemented);
    static_assert(!cntp::PathSwapper<16>::data_migration_implemented);

    ::fixy::report(::fixy::Sink::Out, "test_cntp_path_swap:\n");
    test_admission();
    test_state_machine_and_session_resource_transition();
    test_commit_sender_loses_in_flight_bytes();
    test_concurrent_observer_sees_only_valid_states();
    test_concurrent_complete_receiver_exactly_one_wins();
    test_plan_reader_races_the_writer();
    test_concurrent_begin_swap_keeps_the_winning_plan();
    test_invalid_transition_and_timeout();
    test_deadline_overflow();
    ::fixy::check::pass(kTest, "test_cntp_path_swap: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
