#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

#include <atomic>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>

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

cntp::DeclaredPathSwapPlan make_plan_for(std::uint64_t flow_id, std::uint64_t old_path_id, std::uint64_t new_path_id) {
    auto flow = cntp::admit_path_id(flow_id).value();
    auto old_path = cntp::admit_path_id(old_path_id).value();
    auto new_path = cntp::admit_path_id(new_path_id).value();
    auto timeout = cntp::admit_swap_timeout_ns(1000).value();
    auto plan = cntp::mint_path_swap_plan(flow, old_path, new_path, timeout);
    assert(plan.has_value());
    return *plan;
}

cntp::DeclaredPathSwapPlan make_plan() { return make_plan_for(10, 20, 30); }

// The second plan of the race tests.  Each field differs from the first
// plan, so a read that mixes the two plans names paths that no plan named.
cntp::DeclaredPathSwapPlan make_other_plan() { return make_plan_for(11, 40, 50); }

[[nodiscard]] bool names_one_declared_plan(std::uint64_t flow_id, std::uint64_t old_path_id,
                                           std::uint64_t new_path_id) noexcept {
    return (flow_id == 10 && old_path_id == 20 && new_path_id == 30)
        || (flow_id == 11 && old_path_id == 40 && new_path_id == 50);
}

void test_admission() {
    static_assert(cntp::swap_state_name(cntp::SwapState::Draining) == std::string_view{"Draining"});
    static_assert(cntp::swap_error_name(cntp::SwapError::SamePath) == std::string_view{"SamePath"});
    assert(cntp::swap_state_name(static_cast<cntp::SwapState>(200)) == std::string_view{"<unknown SwapState>"});

    auto zero_path = cntp::admit_path_id(0);
    assert(!zero_path.has_value());
    assert(zero_path.error() == cntp::SwapError::InvalidPathId);

    auto zero_timeout = cntp::admit_swap_timeout_ns(0);
    assert(!zero_timeout.has_value());
    assert(zero_timeout.error() == cntp::SwapError::Timeout);

    auto id = cntp::admit_path_id(1).value();
    auto timeout = cntp::admit_swap_timeout_ns(10).value();
    auto same = cntp::mint_path_swap_plan(id, id, id, timeout);
    assert(!same.has_value());
    assert(same.error() == cntp::SwapError::SamePath);

    auto plan = make_plan();
    assert(plan.value().flow_id().value() == 10);
    assert(plan.value().old_path().value() == 20);
    assert(plan.value().new_path().value() == 30);
    assert(plan.value().timeout_ns().value() == 1000);

    crucible::test::pass("  test_admission: PASSED\n");
}

void test_state_machine_and_session_resource_transition() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<8>(init);
    static_assert(std::is_same_v<decltype(swapper), cntp::PathSwapper<8>>);
    assert(swapper.state() == cntp::SwapState::Stable);
    assert(!swapper.plan(bg).has_value());

    auto plan = make_plan();
    auto begin = swapper.begin_swap(bg, plan, 100);
    assert(begin.has_value());
    assert(swapper.state() == cntp::SwapState::Draining);
    assert(swapper.deadline_ns(bg) == 1100);
    assert(swapper.plan(bg).has_value());
    assert(swapper.plan(bg)->old_path().value() == 20);

    auto bidir = swapper.receiver_accepts_bidir(bg, 200);
    assert(bidir.has_value());
    assert(swapper.state() == cntp::SwapState::BidirReceive);

    auto ack = swapper.sender_observed_drain_ack(bg, 300);
    assert(ack.has_value());
    assert(swapper.state() == cntp::SwapState::NewPathFlushing);

    auto old_handle = sess::mint_session_handle<Proto>(Wire{.id = 1});
    auto swapped = swapper.commit_sender(bg, std::move(old_handle), Wire{.id = 2}, 400);
    assert(swapped.has_value());
    assert(swapper.state() == cntp::SwapState::Complete);
    assert(swapper.event_count(bg) == 4);
    assert(swapper.sequence(bg) == 4);
    assert(swapper.event_at(bg, 3).to == cntp::SwapState::Complete);
    assert(swapper.event_at(bg, 0).flow_id == 10);
    assert(swapped->resource().id == 2);

    auto end = std::move(*swapped).send(42, send_int);
    auto final_wire = std::move(end).close();
    assert(final_wire.id == 2);
    assert(final_wire.last == 42);

    crucible::test::pass("  test_state_machine_and_session_resource_transition: PASSED\n");
}

// The swapper is address-stable and therefore shareable across threads,
// which means its state is read by an observer while a background
// thread is writing it.  A plain enum field would be torn by that, and
// the observer would see a value that is not any of the states.
void test_concurrent_observer_sees_only_valid_states() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<16>(init);

    std::atomic<bool> writer_done{false};
    std::atomic<std::uint32_t> torn_observations{0};
    std::atomic<std::uint32_t> total_observations{0};
    std::jthread observer{[&]() noexcept {
        // The loop runs its body before testing the flag, so at least
        // one observation happens even when the writer finishes first.
        // The claim under test is that no read is torn, not that the
        // observer wins the start-up race.
        do {
            const auto observed = swapper.state();
            total_observations.fetch_add(1, std::memory_order_relaxed);
            switch (observed) {
                case cntp::SwapState::Stable:
                case cntp::SwapState::Draining:
                case cntp::SwapState::BidirReceive:
                case cntp::SwapState::NewPathFlushing:
                case cntp::SwapState::Complete:
                case cntp::SwapState::Failed:
                    break;
                default:
                    torn_observations.fetch_add(1, std::memory_order_relaxed);
            }
        } while (!writer_done.load(std::memory_order_acquire));
    }};

    auto plan = make_plan();
    for (std::uint32_t cycle = 0; cycle < 2000; ++cycle) {
        const std::uint64_t base = static_cast<std::uint64_t>(cycle) * 100;
        assert(swapper.begin_swap(bg, plan, base + 1).has_value());
        assert(swapper.receiver_accepts_bidir(bg, base + 2).has_value());
        assert(swapper.sender_observed_drain_ack(bg, base + 3).has_value());
        assert(swapper.complete_receiver(bg, base + 4).has_value());
    }

    writer_done.store(true, std::memory_order_release);
    observer.join();
    assert(torn_observations.load(std::memory_order_acquire) == 0);
    assert(total_observations.load(std::memory_order_acquire) > 0);

    crucible::test::pass("  test_concurrent_observer_sees_only_valid_states: PASSED\n");
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
    assert(swapper.begin_swap(bg, plan, 100).has_value());
    assert(swapper.receiver_accepts_bidir(bg, 200).has_value());
    assert(swapper.sender_observed_drain_ack(bg, 300).has_value());

    Wire old_wire{.id = 7, .last = 0xBEEF};  // stands in for buffered data
    auto old_handle = sess::mint_session_handle<Proto>(std::move(old_wire));

    Wire new_wire{.id = 9, .last = 0};
    auto swapped = swapper.commit_sender(bg, std::move(old_handle), std::move(new_wire), 400);
    assert(swapped.has_value());
    assert(swapper.state() == cntp::SwapState::Complete);

    // A migration-capable backend would flip the marker and carry the
    // sentinel across, at which point these two lines become a positive
    // check instead of a negative one.
    assert(swapped->resource().id == 9);
    assert(swapped->resource().last == 0);
    assert(swapped->resource().last != 0xBEEF);

    // A session handle must be run to its end.  Dropping one aborts, so
    // the send and close here are the lifetime contract and not extra
    // coverage.
    auto end = std::move(*swapped).send(7, send_int);
    auto final_wire = std::move(end).close();
    assert(final_wire.id == 9);
    assert(final_wire.last == 7);

    crucible::test::pass("  test_commit_sender_loses_in_flight_bytes: PASSED\n");
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
    assert(swapper.begin_swap(bg, plan, 100).has_value());
    assert(swapper.receiver_accepts_bidir(bg, 200).has_value());
    assert(swapper.sender_observed_drain_ack(bg, 300).has_value());
    assert(swapper.event_count(bg) == 3);

    std::atomic<int> winners{0};
    std::atomic<int> losers{0};
    std::atomic<bool> start{false};

    std::jthread racers[kRacers];
    for (int idx = 0; idx < kRacers; ++idx) {
        racers[idx] = std::jthread{[&, idx]() noexcept {
            // The threads are held at this gate so that they enter the
            // exchange having seen the same state.  Without the gate the
            // first thread to wake usually finishes before the others
            // start and the race never happens.
            while (!start.load(std::memory_order_acquire)) {
                // Yielding rather than spinning: this gate is off any
                // hot path, and yielding behaves the same everywhere.
                std::this_thread::yield();
            }
            const std::uint64_t now_ns = 400 + static_cast<std::uint64_t>(idx);
            auto result = swapper.complete_receiver(bg, now_ns);
            if (result.has_value()) {
                winners.fetch_add(1, std::memory_order_relaxed);
            } else {
                assert(result.error() == cntp::SwapError::InvalidTransition);
                losers.fetch_add(1, std::memory_order_relaxed);
            }
        }};
    }

    start.store(true, std::memory_order_release);
    for (auto& racer : racers)
        racer.join();

    assert(winners.load() == 1);
    assert(losers.load() == kRacers - 1);

    // One winner is not enough on its own: the log must have gained one
    // event and not one per thread that observed the source state.
    assert(swapper.event_count(bg) == 4);
    assert(swapper.event_at(bg, 3).from == cntp::SwapState::NewPathFlushing);
    assert(swapper.event_at(bg, 3).to == cntp::SwapState::Complete);
    assert(swapper.state() == cntp::SwapState::Complete);

    crucible::test::pass("  test_concurrent_complete_receiver_exactly_one_wins: PASSED\n");
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

    std::atomic<bool> writer_done{false};
    std::atomic<std::uint32_t> mixed_reads{0};
    std::jthread reader{[&]() noexcept {
        do {
            if (const auto seen = swapper.plan(bg); seen.has_value()) {
                if (!names_one_declared_plan(seen->flow_id().value(), seen->old_path().value(),
                                             seen->new_path().value())) {
                    mixed_reads.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (swapper.event_count(bg) > 0) {
                // The latest event.  The ring holds each event at its
                // sequence number minus one, modulo the ring size.
                const auto event = swapper.event_at(bg, swapper.sequence(bg) - 1);
                if (!names_one_declared_plan(event.flow_id, event.old_path, event.new_path)
                    || !cntp::is_valid_path_swap_transition(event.from, event.to)) {
                    mixed_reads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        } while (!writer_done.load(std::memory_order_acquire));
    }};

    for (std::uint32_t cycle = 0; cycle < 2000; ++cycle) {
        const std::uint64_t base = static_cast<std::uint64_t>(cycle) * 100;
        const auto& plan = (cycle % 2u == 0u) ? first : second;
        assert(swapper.begin_swap(bg, plan, base + 1).has_value());
        assert(swapper.receiver_accepts_bidir(bg, base + 2).has_value());
        assert(swapper.sender_observed_drain_ack(bg, base + 3).has_value());
        assert(swapper.complete_receiver(bg, base + 4).has_value());
    }

    writer_done.store(true, std::memory_order_release);
    reader.join();
    assert(mixed_reads.load(std::memory_order_acquire) == 0);

    crucible::test::pass("  test_plan_reader_races_the_writer: PASSED\n");
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
        std::atomic<bool> start{false};
        std::atomic<int> winners{0};
        auto race = [&](cntp::DeclaredPathSwapPlan const& plan) noexcept {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if (swapper.begin_swap(bg, plan, 100).has_value()) {
                winners.fetch_add(1, std::memory_order_relaxed);
            }
        };
        {
            std::jthread left{race, std::cref(first)};
            std::jthread right{race, std::cref(second)};
            start.store(true, std::memory_order_release);
        }
        assert(winners.load(std::memory_order_acquire) == 1);
        const auto held = swapper.plan(bg);
        assert(held.has_value());
        assert(swapper.event_count(bg) == 1);
        const auto event = swapper.event_at(bg, 0);
        assert(event.flow_id == held->flow_id().value());
        assert(event.old_path == held->old_path().value());
        assert(event.new_path == held->new_path().value());
    }

    crucible::test::pass("  test_concurrent_begin_swap_keeps_the_winning_plan: PASSED\n");
}

void test_invalid_transition_and_timeout() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<4>(init);
    auto bad_ack = swapper.sender_observed_drain_ack(bg, 1);
    assert(!bad_ack.has_value());
    assert(bad_ack.error() == cntp::SwapError::InvalidTransition);
    assert(swapper.event_count(bg) == 0);

    auto plan = make_plan();
    auto begin = swapper.begin_swap(bg, plan, 10);
    assert(begin.has_value());
    auto again = swapper.begin_swap(bg, plan, 11);
    assert(!again.has_value());
    assert(again.error() == cntp::SwapError::InvalidTransition);

    auto timeout = swapper.receiver_accepts_bidir(bg, 2000);
    assert(!timeout.has_value());
    assert(timeout.error() == cntp::SwapError::Timeout);
    assert(swapper.state() == cntp::SwapState::Failed);

    crucible::test::pass("  test_invalid_transition_and_timeout: PASSED\n");
}

void test_deadline_overflow() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};

    auto swapper = cntp::mint_path_swapper<4>(init);
    auto plan = make_plan();
    auto overflow = swapper.begin_swap(bg, plan, std::numeric_limits<std::uint64_t>::max() - 10);
    assert(!overflow.has_value());
    assert(overflow.error() == cntp::SwapError::DeadlineOverflow);
    assert(swapper.state() == cntp::SwapState::Stable);
    assert(!swapper.plan(bg).has_value());

    crucible::test::pass("  test_deadline_overflow: PASSED\n");
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
    crucible::test::pass("test_cntp_path_swap: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
