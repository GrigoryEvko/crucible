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

void send_int(Wire& wire, int value) noexcept {
    wire.last = value;
}

cntp::DeclaredPathSwapPlan make_plan() {
    auto flow = cntp::admit_path_id(10).value();
    auto old_path = cntp::admit_path_id(20).value();
    auto new_path = cntp::admit_path_id(30).value();
    auto timeout = cntp::admit_swap_timeout_ns(1'000).value();
    auto plan = cntp::mint_path_swap_plan(flow, old_path, new_path, timeout);
    assert(plan.has_value());
    return *plan;
}

void test_admission() {
    assert(cntp::swap_state_name(cntp::SwapState::Draining) ==
           std::string_view{"Draining"});
    assert(cntp::swap_error_name(cntp::SwapError::SamePath) ==
           std::string_view{"SamePath"});

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
    assert(swapper.deadline_ns() == 1'100);

    auto bidir = swapper.receiver_accepts_bidir(bg, 200);
    assert(bidir.has_value());
    assert(swapper.state() == cntp::SwapState::BidirReceive);

    auto ack = swapper.sender_observed_drain_ack(bg, 300);
    assert(ack.has_value());
    assert(swapper.state() == cntp::SwapState::NewPathFlushing);

    auto old_handle = proto::mint_session_handle<Proto>(Wire{.id = 1});
    auto swapped = swapper.commit_sender(bg, std::move(old_handle),
                                         Wire{.id = 2}, 400);
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

void test_concurrent_observer_sees_only_valid_states() {
    // fixy-A5-038 regression: PathSwapper inherits Pinned, advertising
    // address-stable cross-thread sharing.  Pre-fix the state_ field was a
    // plain `SwapState` enum read non-atomically by state() — a foreground
    // observer racing against a Bg-context transition_to would see torn
    // bytes and TSan would flame.  Atomic state_ + acquire/release pairing
    // makes the observation sound.
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};

    auto swapper = cntp::mint_path_swapper<16>(init);

    std::atomic<bool> writer_done{false};
    std::atomic<std::uint32_t> torn_observations{0};
    std::atomic<std::uint32_t> total_observations{0};
    std::thread observer{[&]() noexcept {
        // do-while ensures at least one observation even if writer races to
        // completion before the observer thread first wakes — the structural
        // claim is "no torn read", not "observer wins the start-up race".
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

// fixy-A5-024 HS14 fixture #2: runtime witness that `commit_sender` is
// state-machine-only — bytes sitting on the OLD resource at the swap
// boundary are LOST, never migrated to the new resource.  A real Tier-2
// engine (MPTCP TCP_MIGRATE, RDMA path-migration verb, AF_XDP map swap)
// would replay or hand-off these bytes; today's stub does not, and the
// `data_migration_implemented` honesty marker advertises that fact.
//
// We arm a sentinel byte (`old_wire.last = 0xBEEF`) on the OLD wire to
// represent "buffered-but-unsent" application data, drive the swap to
// completion, and then assert the NEW wire is structurally independent:
// fresh id, no inherited sentinel.  The OLD wire is consumed by the
// detach inside commit_sender and is unobservable post-call — the
// structural type guarantee IS the proof that the bytes are gone.
void test_commit_sender_loses_in_flight_bytes() {
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};

    auto swapper = cntp::mint_path_swapper<8>(init);
    auto plan = make_plan();
    assert(swapper.begin_swap(bg, plan, 100).has_value());
    assert(swapper.receiver_accepts_bidir(bg, 200).has_value());
    assert(swapper.sender_observed_drain_ack(bg, 300).has_value());

    Wire old_wire{.id = 7, .last = 0xBEEF};  // sentinel "in-flight byte"
    auto old_handle = proto::mint_session_handle<Proto>(std::move(old_wire));

    Wire new_wire{.id = 9, .last = 0};
    auto swapped = swapper.commit_sender(bg, std::move(old_handle),
                                         std::move(new_wire), 400);
    assert(swapped.has_value());
    assert(swapper.state() == cntp::SwapState::Complete);

    // The new resource is structurally independent of the old one: the
    // sentinel byte 0xBEEF is unrecoverable because no Tier-2 migration
    // engine copied or replayed it.  A future migration-capable backend
    // would flip `data_migration_implemented` to true AND propagate the
    // sentinel — that day this assert turns into a positive check.
    assert(swapped->resource().id == 9);
    assert(swapped->resource().last == 0);
    assert(swapped->resource().last != 0xBEEF);

    // Drive the session through send+close so the SessionHandle lifetime
    // contract is honored (the framework aborts on dropped handles).
    auto end = std::move(*swapped).send(7, send_int);
    auto final_wire = std::move(end).close();
    assert(final_wire.id == 9);
    assert(final_wire.last == 7);

    std::printf("  test_commit_sender_loses_in_flight_bytes: PASSED\n");
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

    auto timeout = swapper.receiver_accepts_bidir(bg, 2'000);
    assert(!timeout.has_value());
    assert(timeout.error() == cntp::SwapError::Timeout);
    assert(swapper.state() == cntp::SwapState::Failed);

    std::printf("  test_invalid_transition_and_timeout: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositivePathId) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredPathSwapPlan) ==
                  sizeof(cntp::PathSwapPlan));
    static_assert(cntp::CtxFitsPathSwapMint<crucible::effects::ColdInitCtx>);
    static_assert(cntp::CtxFitsPathSwapTransition<crucible::effects::BgDrainCtx>);
    static_assert(!cntp::CtxFitsPathSwapTransition<crucible::effects::HotFgCtx>);
    static_assert(cntp::PathSwapSessionResource<Wire>);
    static_assert(!cntp::PathSwapSessionResource<Wire&>);

    // fixy-A5-024 HS14 fixture #1: compile-time witness that the
    // PathSwapper class advertises STATE-ONLY semantics — no Tier-2
    // in-flight data migration engine (MPTCP/RDMA/AF_XDP) ships yet.
    // The marker is `static constexpr bool`, grep-discoverable via
    // `data_migration_implemented`, and reachable from every template
    // arity so callers can branch at compile time on the gap.
    static_assert(!cntp::PathSwapper<>::data_migration_implemented,
                  "fixy-A5-024: PathSwapper must advertise state-only "
                  "semantics until a per-transport migration engine ships");
    static_assert(!cntp::PathSwapper<8>::data_migration_implemented);
    static_assert(!cntp::PathSwapper<16>::data_migration_implemented);

    std::printf("test_cntp_path_swap:\n");
    test_admission();
    test_state_machine_and_session_resource_transition();
    test_commit_sender_loses_in_flight_bytes();
    test_concurrent_observer_sees_only_valid_states();
    test_invalid_transition_and_timeout();
    std::printf("test_cntp_path_swap: all PASSED\n");
    return 0;
}
