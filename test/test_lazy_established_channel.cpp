#include <crucible/handles/LazyEstablishedChannel.h>

#include <atomic>
#include <cstdio>
#include <optional>
#include <thread>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

struct VesselChannel {
    int sentinel = 0;
    int call_count = 0;
};

// A loop of sends is the shape a dispatch-path channel takes.
using DispatchProto = Loop<Select<Send<int, Continue>, End>>;

using Channel = LazyEstablishedChannel<DispatchProto, VesselChannel>;

// These repeat the header's self-tests from a consumer translation
// unit, where the project warning flags apply.

static_assert(!std::is_copy_constructible_v<Channel>);
static_assert(!std::is_move_constructible_v<Channel>);
static_assert(std::is_base_of_v<Pinned<Channel>, Channel>);
static_assert(std::is_same_v<typename Channel::protocol, DispatchProto>);
static_assert(std::is_same_v<typename Channel::resource_type, VesselChannel>);

static_assert(sizeof(Channel) == sizeof(std::atomic<VesselChannel*>));

int run_pre_establish_observe_returns_nullopt() {
    Channel ch;
    if (ch.is_established()) return 1;

    auto h = ch.observe();
    if (h.has_value()) return 2;
    return 0;
}

int run_establish_then_observe_yields_handle() {
    Channel ch;
    VesselChannel storage{42, 0};

    ch.establish(&storage);
    if (!ch.is_established()) return 1;

    auto h = ch.observe();
    if (!h.has_value()) return 2;

    // Resource is VesselChannel*, so resource() yields the pointer.
    if (h->resource() != &storage) return 3;
    if (h->resource()->sentinel != 42) return 4;

    // A handle abandoned without detach fires its own contract.
    std::move(*h).detach(detach_reason::TestInstrumentation{});
    return 0;
}

int run_drive_protocol_after_observe() {
    Channel ch;
    VesselChannel storage{0, 0};
    ch.establish(&storage);

    auto h = ch.observe();
    if (!h) return 1;

    // Branch 0 is the Send arm, so the handle becomes Send<int, Continue>.
    auto send_handle = std::move(*h).select_local<0>();

    int side_effect = 0;
    auto next = std::move(send_handle).send(99, [&side_effect](VesselChannel*& c, int v) noexcept {
        c->sentinel = v;
        c->call_count++;
        side_effect = 1;
    });
    if (side_effect != 1) return 2;
    if (storage.sentinel != 99) return 3;
    if (storage.call_count != 1) return 4;

    // Continue has resolved, so `next` sits at the loop body's head.
    std::move(next).detach(detach_reason::TestInstrumentation{});
    return 0;
}

int run_multiple_observers_share_resource() {
    Channel ch;
    VesselChannel storage{7, 0};
    ch.establish(&storage);

    auto h1 = ch.observe();
    auto h2 = ch.observe();
    auto h3 = ch.observe();

    if (!h1 || !h2 || !h3) return 1;

    if (h1->resource() != &storage) return 2;
    if (h2->resource() != &storage) return 3;
    if (h3->resource() != &storage) return 4;

    // Each handle is independent, so each one detaches on its own.
    std::move(*h1).detach(detach_reason::TestInstrumentation{});
    std::move(*h2).detach(detach_reason::TestInstrumentation{});
    std::move(*h3).detach(detach_reason::TestInstrumentation{});
    return 0;
}

int run_protocol_name_static() {
    auto name = Channel::protocol_name();
    if (name.empty()) return 1;
    if (name.find("Loop") == std::string_view::npos) return 2;
    if (name.find("Select") == std::string_view::npos) return 3;
    if (name.find("Send") == std::string_view::npos) return 4;
    return 0;
}

// The startup pattern this models: one thread initialises the channel
// and publishes it, while a worker polls observe(), takes a not-ready
// path before publication, and drives the protocol after.

int run_worked_example_vessel_startup() {
    Channel dispatch_channel;
    VesselChannel storage{0, 0};
    std::atomic<int> worker_processed{0};
    std::atomic<int> worker_fell_back{0};

    auto worker = std::jthread([&] {
        // The poll loop is unbounded on purpose.  The init thread below
        // calls establish() unconditionally and then joins this worker,
        // so observe() does succeed and the loop does return.  A fixed
        // poll budget would be a timeout in disguise: under scheduling
        // pressure the worker could exhaust it before the publisher
        // reaches establish() and fail for no reason.  This loop hangs
        // only if the publish or observe path is itself broken, which
        // is a real bug and not a scheduling flake.
        for (;;) {
            auto h = dispatch_channel.observe();
            if (!h) {
                worker_fell_back.fetch_add(1, std::memory_order_relaxed);
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            auto send_handle = std::move(*h).select_local<0>();
            auto next =
                std::move(send_handle).send(worker_processed.load() + 1, [&](VesselChannel*& c, int v) noexcept {
                    c->sentinel = v;
                    c->call_count++;
                });
            std::move(next).detach(detach_reason::TestInstrumentation{});
            worker_processed.fetch_add(1, std::memory_order_release);
            return;
        }
    });

    // The publish is a store-release, so the worker's load-acquire in
    // observe() sees the storage fully initialised.
    storage.sentinel = 1;
    dispatch_channel.establish(&storage);

    worker.join();

    if (worker_processed.load() != 1) return 1;
    // The fallback count is deliberately not asserted: any number of
    // fallbacks, zero included, is a correct schedule.
    if (storage.sentinel != 1) return 2;  // the worker rewrote it with 1
    if (storage.call_count != 1) return 3;
    return 0;
}

int run_observer_before_establish_falls_back() {
    Channel ch;

    int fallback_count = 0;
    {
        auto h = ch.observe();
        if (h.has_value()) return 1;
        fallback_count++;
    }
    if (fallback_count != 1) return 2;

    VesselChannel storage{};
    ch.establish(&storage);

    auto h = ch.observe();
    if (!h) return 3;
    std::move(*h).detach(detach_reason::TestInstrumentation{});
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_pre_establish_observe_returns_nullopt(); rc != 0) return rc;
    if (int rc = run_establish_then_observe_yields_handle(); rc != 0) return 100 + rc;
    if (int rc = run_drive_protocol_after_observe(); rc != 0) return 200 + rc;
    if (int rc = run_multiple_observers_share_resource(); rc != 0) return 300 + rc;
    if (int rc = run_protocol_name_static(); rc != 0) return 400 + rc;
    if (int rc = run_worked_example_vessel_startup(); rc != 0) return 500 + rc;
    if (int rc = run_observer_before_establish_falls_back(); rc != 0) return 600 + rc;

    std::puts("lazy_established_channel: pre/post observe + multi-observer + Vessel-startup OK");
    return 0;
}
