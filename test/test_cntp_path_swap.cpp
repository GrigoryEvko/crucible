#include <crucible/cntp/PathSwap.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <string_view>
#include <thread>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace proto = crucible::safety::proto;

namespace {

struct Wire {
    int id = 0;
    int last = 0;
};

using Proto = proto::Send<int, proto::End>;

void send_int(Wire& wire, int value) noexcept { wire.last = value; }

cntp::DeclaredPathSwapPlan make_plan() {
    auto flow = cntp::admit_path_id(10).value();
    auto old_path = cntp::admit_path_id(20).value();
    auto new_path = cntp::admit_path_id(30).value();
    auto timeout = cntp::admit_swap_timeout_ns(1000).value();
    auto plan = cntp::mint_path_swap_plan(flow, old_path, new_path, timeout);
    assert(plan.has_value());
    return *plan;
}

void test_admission() {
    assert(cntp::swap_state_name(cntp::SwapState::Draining) == std::string_view{"Draining"});
    assert(cntp::swap_error_name(cntp::SwapError::SamePath) == std::string_view{"SamePath"});

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

    std::printf("  test_admission: PASSED\n");
}

void test_state_machine_and_session_resource_transition() {
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};

    auto swapper = cntp::mint_path_swapper<8>(init);
    static_assert(std::is_same_v<decltype(swapper), cntp::PathSwapper<8>>);
    assert(swapper.state() == cntp::SwapState::Stable);

    auto plan = make_plan();
    auto begin = swapper.begin_swap(bg, plan, 100);
    assert(begin.has_value());
    assert(swapper.state() == cntp::SwapState::Draining);
    assert(swapper.deadline_ns() == 1100);

    auto bidir = swapper.receiver_accepts_bidir(bg, 200);
    assert(bidir.has_value());
    assert(swapper.state() == cntp::SwapState::BidirReceive);

    auto ack = swapper.sender_observed_drain_ack(bg, 300);
    assert(ack.has_value());
    assert(swapper.state() == cntp::SwapState::NewPathFlushing);

    auto old_handle = proto::mint_session_handle<Proto>(Wire{.id = 1});
    auto swapped = swapper.commit_sender(bg, std::move(old_handle), Wire{.id = 2}, 400);
    assert(swapped.has_value());
    assert(swapper.state() == cntp::SwapState::Complete);
    assert(swapper.event_count() == 4);
    assert(swapper.event_at(3).to == cntp::SwapState::Complete);
    assert(swapped->resource().id == 2);

    auto end = std::move(*swapped).send(42, send_int);
    auto final_wire = std::move(end).close();
    assert(final_wire.id == 2);
    assert(final_wire.last == 42);

    std::printf("  test_state_machine_and_session_resource_transition: PASSED\n");
}

// The swapper is address-stable and therefore shareable across threads,
// which means its state is read by an observer while a background
// thread is writing it.  A plain enum field would be torn by that, and
// the observer would see a value that is not any of the states.
void test_concurrent_observer_sees_only_valid_states() {
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};

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

    std::printf("  test_concurrent_observer_sees_only_valid_states: PASSED\n");
}

// Committing a swap moves the state machine and nothing else.  Data
// still buffered on the old resource is dropped, where a transport that
// could genuinely migrate a path would replay or hand it over.  The
// sentinel value planted below stands in for such buffered data, and
// the old resource is consumed by the commit, so its absence from the
// new one is the whole proof: there is no path by which it could
// arrive.
void test_commit_sender_loses_in_flight_bytes() {
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};

    auto swapper = cntp::mint_path_swapper<8>(init);
    auto plan = make_plan();
    assert(swapper.begin_swap(bg, plan, 100).has_value());
    assert(swapper.receiver_accepts_bidir(bg, 200).has_value());
    assert(swapper.sender_observed_drain_ack(bg, 300).has_value());

    Wire old_wire{.id = 7, .last = 0xBEEF};  // stands in for buffered data
    auto old_handle = proto::mint_session_handle<Proto>(std::move(old_wire));

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

    std::printf("  test_commit_sender_loses_in_flight_bytes: PASSED\n");
}

// A transition that read the state and then stored it would let two
// threads both see the same source state and both append an event, so
// one edge would appear twice in the audit log.  Replacing that with a
// single compare-and-exchange makes exactly one thread win the edge.
// The losers are then refused, because the state they would transition
// from is no longer the state that is there.
void test_concurrent_complete_receiver_exactly_one_wins() {
    constexpr int kRacers = 16;
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};

    auto swapper = cntp::mint_path_swapper<32>(init);
    auto plan = make_plan();

    // The three setup transitions run on this thread alone, so the
    // event count is known exactly before the race begins and the one
    // event the race is allowed to add can be counted afterwards.
    assert(swapper.begin_swap(bg, plan, 100).has_value());
    assert(swapper.receiver_accepts_bidir(bg, 200).has_value());
    assert(swapper.sender_observed_drain_ack(bg, 300).has_value());
    assert(swapper.event_count() == 3);

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
    for (auto& t : racers)
        t.join();

    assert(winners.load() == 1);
    assert(losers.load() == kRacers - 1);

    // One winner is not enough on its own: the log must have gained one
    // event and not one per thread that observed the source state.
    assert(swapper.event_count() == 4);
    assert(swapper.event_at(3).from == cntp::SwapState::NewPathFlushing);
    assert(swapper.event_at(3).to == cntp::SwapState::Complete);
    assert(swapper.state() == cntp::SwapState::Complete);

    std::printf("  test_concurrent_complete_receiver_exactly_one_wins: PASSED\n");
}

void test_invalid_transition_and_timeout() {
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};

    auto swapper = cntp::mint_path_swapper<4>(init);
    auto bad_ack = swapper.sender_observed_drain_ack(bg, 1);
    assert(!bad_ack.has_value());
    assert(bad_ack.error() == cntp::SwapError::InvalidTransition);

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

    std::printf("  test_invalid_transition_and_timeout: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositivePathId) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredPathSwapPlan) == sizeof(cntp::PathSwapPlan));
    static_assert(cntp::CtxFitsPathSwapMint<crucible::effects::ColdInitCtx>);
    static_assert(cntp::CtxFitsPathSwapTransition<crucible::effects::BgDrainCtx>);
    static_assert(!cntp::CtxFitsPathSwapTransition<crucible::effects::HotFgCtx>);
    static_assert(cntp::PathSwapSessionResource<Wire>);
    static_assert(!cntp::PathSwapSessionResource<Wire&>);

    // The marker is readable at every arity, so a caller can branch on
    // the gap at compile time rather than discovering it at runtime.
    static_assert(!cntp::PathSwapper<>::data_migration_implemented, "PathSwapper must advertise state-only semantics "
                                                                    "until a per-transport migration engine ships");
    static_assert(!cntp::PathSwapper<8>::data_migration_implemented);
    static_assert(!cntp::PathSwapper<16>::data_migration_implemented);

    std::printf("test_cntp_path_swap:\n");
    test_admission();
    test_state_machine_and_session_resource_transition();
    test_commit_sender_loses_in_flight_bytes();
    test_concurrent_observer_sees_only_valid_states();
    test_concurrent_complete_receiver_exactly_one_wins();
    test_invalid_transition_and_timeout();
    std::printf("test_cntp_path_swap: all PASSED\n");
    return 0;
}
