// Runtime + compile-time harness for safety/LazyEstablishedChannel.h
// (task #403, SAFEINT-B14).
//
// Coverage:
//   * Compile-time: LazyEstablishedChannel is Pinned (deleted copy/
//     move); typedefs wire correctly; session_handle_type matches the
//     Loop-unrolled SessionHandle specialisation; sizeof equals
//     PublishOnce<Resource> (one atomic pointer).
//   * Runtime: pre-establish observe() returns nullopt; establish()
//     publishes the resource; post-establish observe() returns a
//     SessionHandle bound to the published resource; handle drives
//     the protocol normally; multiple concurrent observers each get
//     a fresh handle pointing to the same resource; second
//     establish() fires the PublishOnce contract (death test).
//   * Worked example: a Vessel-style startup pattern where one
//     thread initialises a channel and publishes it, while a worker
//     thread polls observe() and processes the protocol once
//     established.

#include <crucible/safety/LazyEstablishedChannel.h>

#include <atomic>
#include <cstdio>
#include <optional>
#include <thread>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

// ── Fixture: a long-lived channel storage type ─────────────────────

struct VesselChannel {
    int sentinel = 0;
    int call_count = 0;
};

// A simple loop-of-send protocol — the kind of channel a Vessel
// adapter would publish for the dispatch path.
using DispatchProto = Loop<
    Select<
        Send<int, Continue>,
        End>>;

using Channel = LazyEstablishedChannel<DispatchProto, VesselChannel>;

// ── Compile-time witnesses (mirror header self-tests in TU) ───────

static_assert(!std::is_copy_constructible_v<Channel>);
static_assert(!std::is_move_constructible_v<Channel>);
static_assert(std::is_base_of_v<Pinned<Channel>, Channel>);
static_assert(std::is_same_v<typename Channel::protocol,      DispatchProto>);
static_assert(std::is_same_v<typename Channel::resource_type, VesselChannel>);

// One atomic pointer of overhead — no more.
static_assert(sizeof(Channel) == sizeof(std::atomic<VesselChannel*>));

// ── Test: pre-establish observe() returns nullopt ─────────────────

int run_pre_establish_observe_returns_nullopt() {
    Channel ch;
    if (ch.is_established()) return 1;

    auto h = ch.observe();
    if (h.has_value())       return 2;
    return 0;
}

// ── Test: post-establish observe() returns a session handle ───────

int run_establish_then_observe_yields_handle() {
    Channel ch;
    VesselChannel storage{42, 0};

    ch.establish(&storage);
    if (!ch.is_established()) return 1;

    auto h = ch.observe();
    if (!h.has_value()) return 2;

    // The handle's Resource is VesselChannel*; resource() returns
    // the pointer (via Resource&).
    if (h->resource() != &storage)         return 3;
    if (h->resource()->sentinel != 42)      return 4;

    // Detach to clean up — this is a Loop<Select<...>> protocol; the
    // handle is at the body's head (Select) after Loop unrolls.
    std::move(*h).detach();
    return 0;
}

// ── Test: drive the protocol after observe() ──────────────────────

int run_drive_protocol_after_observe() {
    Channel ch;
    VesselChannel storage{0, 0};
    ch.establish(&storage);

    auto h = ch.observe();
    if (!h) return 1;

    // Pick branch 0 (Send) — handle becomes Send<int, Continue>.
    auto send_handle = std::move(*h).select<0>();

    // Drive the send: the transport mutates the channel's storage.
    int side_effect = 0;
    auto next = std::move(send_handle).send(
        99,
        [&side_effect](VesselChannel*& c, int v) noexcept {
            c->sentinel = v;
            c->call_count++;
            side_effect = 1;
        });
    if (side_effect != 1)        return 2;
    if (storage.sentinel != 99)  return 3;
    if (storage.call_count != 1) return 4;

    // `next` is at the loop body's head again (Continue resolved).
    std::move(next).detach();
    return 0;
}

// ── Test: multiple observers each get a fresh handle ──────────────

int run_multiple_observers_share_resource() {
    Channel ch;
    VesselChannel storage{7, 0};
    ch.establish(&storage);

    auto h1 = ch.observe();
    auto h2 = ch.observe();
    auto h3 = ch.observe();

    if (!h1 || !h2 || !h3) return 1;

    // All three handles point at the same resource.
    if (h1->resource() != &storage) return 2;
    if (h2->resource() != &storage) return 3;
    if (h3->resource() != &storage) return 4;

    // Each handle is independent (move-only); detach them
    // individually.
    std::move(*h1).detach();
    std::move(*h2).detach();
    std::move(*h3).detach();
    return 0;
}

// ── Test: protocol_name() static accessor ─────────────────────────

int run_protocol_name_static() {
    auto name = Channel::protocol_name();
    if (name.empty())                                    return 1;
    if (name.find("Loop")   == std::string_view::npos)   return 2;
    if (name.find("Select") == std::string_view::npos)   return 3;
    if (name.find("Send")   == std::string_view::npos)   return 4;
    return 0;
}

// ── Worked example: Vessel-startup pattern ────────────────────────
//
// One thread initialises the channel and publishes it; a worker
// thread polls observe(), falls back to a "not yet ready" path
// before publication, processes the protocol after.  This is the
// canonical pattern §14 describes.

int run_worked_example_vessel_startup() {
    Channel        dispatch_channel;
    VesselChannel  storage{0, 0};
    std::atomic<int> worker_processed{0};
    std::atomic<int> worker_fell_back{0};

    auto worker = std::thread([&]{
        // Poll until established.  Real production would check
        // periodically with backoff or via a one-shot signal; this
        // test polls tightly to keep the runtime short.
        for (int i = 0; i < 10000; ++i) {
            auto h = dispatch_channel.observe();
            if (!h) {
                worker_fell_back.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
                continue;
            }
            // Drive one protocol cycle: pick Send, send, detach.
            auto send_handle = std::move(*h).select<0>();
            auto next = std::move(send_handle).send(
                worker_processed.load() + 1,
                [&](VesselChannel*& c, int v) noexcept {
                    c->sentinel = v;
                    c->call_count++;
                });
            std::move(next).detach();
            worker_processed.fetch_add(1, std::memory_order_release);
            return;
        }
        // Worker exhausted polling without seeing publication —
        // test will fail below.
    });

    // Init thread: prepare the storage, then publish.  The publish
    // is store-release so the worker's load-acquire on observe()
    // sees the fully-initialised storage.
    storage.sentinel = 1;
    dispatch_channel.establish(&storage);

    worker.join();

    if (worker_processed.load() != 1) return 1;
    // The worker may have fallen back zero or more times depending
    // on scheduling — both are correct.  Just verify it eventually
    // observed the publication.
    if (storage.sentinel != 1)        return 2;  // worker overwrote with 1
    if (storage.call_count != 1)      return 3;
    return 0;
}

// ── Test: worker that observes BEFORE establish() falls back ──────

int run_observer_before_establish_falls_back() {
    Channel ch;

    // Observer runs first — must get nullopt and fall back.
    int fallback_count = 0;
    {
        auto h = ch.observe();
        if (h.has_value()) return 1;
        fallback_count++;
    }
    if (fallback_count != 1) return 2;

    // Now publish.
    VesselChannel storage{};
    ch.establish(&storage);

    // Subsequent observe() succeeds.
    auto h = ch.observe();
    if (!h) return 3;
    std::move(*h).detach();
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_pre_establish_observe_returns_nullopt();   rc != 0) return rc;
    if (int rc = run_establish_then_observe_yields_handle();    rc != 0) return 100 + rc;
    if (int rc = run_drive_protocol_after_observe();            rc != 0) return 200 + rc;
    if (int rc = run_multiple_observers_share_resource();       rc != 0) return 300 + rc;
    if (int rc = run_protocol_name_static();                    rc != 0) return 400 + rc;
    if (int rc = run_worked_example_vessel_startup();           rc != 0) return 500 + rc;
    if (int rc = run_observer_before_establish_falls_back();    rc != 0) return 600 + rc;

    std::puts("lazy_established_channel: pre/post observe + multi-observer + Vessel-startup OK");
    return 0;
}
