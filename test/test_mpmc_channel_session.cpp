// Every session here carries an empty permission set, so no permission-set
// evolution is exercised.  The protocol has no branch, so no convergence
// check applies either.

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/MpmcChannelSession.h>

namespace {

// A dedicated tag tree, so this channel cannot collide with any other
// instantiation.
struct TestChannelTag {};

using Channel = ::crucible::concurrent::PermissionedMpmcChannel<int, 1024, TestChannelTag>;

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            ++total_failed;                                                                    \
            return;                                                                            \
        }                                                                                      \
    } while (0)

template <typename Body>
void run_test(const char* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

namespace ses = ::crucible::safety::proto::mpmc_channel_session;

static_assert(ses::MpmcChannelSessionSurface<Channel>, "production PermissionedMpmcChannel must satisfy "
                                                       "MpmcChannelSessionSurface");

using ProducerProtoInt = ses::ProducerProto<int>;
using ConsumerProtoInt = ses::ConsumerProto<int>;

static_assert(
    std::is_same_v<ProducerProtoInt, ::crucible::safety::proto::Loop<
                                         ::crucible::safety::proto::Send<int, ::crucible::safety::proto::Continue>>>);

static_assert(
    std::is_same_v<ConsumerProtoInt, ::crucible::safety::proto::Loop<
                                         ::crucible::safety::proto::Recv<int, ::crucible::safety::proto::Continue>>>);

// A copied handle would double-count its share of the pool refcount.
static_assert(!std::is_copy_constructible_v<Channel::ProducerHandle>);
static_assert(std::is_move_constructible_v<Channel::ProducerHandle>);
static_assert(!std::is_copy_constructible_v<Channel::ConsumerHandle>);
static_assert(std::is_move_constructible_v<Channel::ConsumerHandle>);

// The session header asserts the same size equality on its own witness
// instantiation.  Re-asserting under this translation unit's build flags
// catches drift between that witness and the target instantiation.

namespace witness {
namespace proto = ::crucible::safety::proto;
using PSH_End_Prod = proto::PermissionedSessionHandle<proto::End, proto::EmptyPermSet, Channel::ProducerHandle*>;
using SH_End_Prod = proto::SessionHandle<proto::End, Channel::ProducerHandle*>;
static_assert(sizeof(PSH_End_Prod) == sizeof(SH_End_Prod), "PermissionedSessionHandle<End> over a producer handle "
                                                           "pointer must be the size of the bare SessionHandle.");

using PSH_End_Cons = proto::PermissionedSessionHandle<proto::End, proto::EmptyPermSet, Channel::ConsumerHandle*>;
using SH_End_Cons = proto::SessionHandle<proto::End, Channel::ConsumerHandle*>;
static_assert(sizeof(PSH_End_Cons) == sizeof(SH_End_Cons));
}  // namespace witness

// With one producer and one consumer the ring orders as a single-producer
// single-consumer queue, so arrival order may be asserted here.  Order
// across several producers is not guaranteed, so the multi-producer case
// asserts set equality instead.

void test_typed_session_single_round_trip() {
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    Channel ch;

    auto p_opt = ch.producer();
    auto c_opt = ch.consumer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());
    CRUCIBLE_TEST_REQUIRE(c_opt.has_value());

    auto prod_handle = std::move(*p_opt);
    auto cons_handle = std::move(*c_opt);

    constexpr int kCount = 1024;
    std::atomic<bool> producer_done{false};
    std::vector<int> received;
    received.reserve(kCount);

    std::jthread producer{[&prod_handle, &producer_done](auto) mutable {
        auto psh = ses::mint_mpmc_producer_session<Channel>(::crucible::effects::HotFgCtx{}, prod_handle);
        for (int i = 0; i < kCount; ++i) {
            auto next = std::move(psh).send(i, ses::blocking_push);
            psh = std::move(next);
        }
        std::move(psh).detach(TestInstrumentation{});
        producer_done.store(true, std::memory_order_release);
    }};

    std::jthread consumer{[&cons_handle, &received](auto) mutable {
        auto psh = ses::mint_mpmc_consumer_session<Channel>(::crucible::effects::HotFgCtx{}, cons_handle);
        for (int i = 0; i < kCount; ++i) {
            auto [v, next] = std::move(psh).recv(ses::blocking_pop);
            received.push_back(v);
            psh = std::move(next);
        }
        std::move(psh).detach(TestInstrumentation{});
    }};

    producer.join();
    consumer.join();

    CRUCIBLE_TEST_REQUIRE(producer_done.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(received.size() == static_cast<std::size_t>(kCount));
    for (std::size_t i = 0; i < static_cast<std::size_t>(kCount); ++i) {
        CRUCIBLE_TEST_REQUIRE(received[i] == static_cast<int>(i));
    }
}

// The queue guarantees livelock-free progress under contention, which is
// what licenses the unbounded spin in the drain loop below.

void test_typed_session_multi_round_trip() {
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    Channel ch;

    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 256;
    constexpr int kTotal = kProducers * kPerProducer;

    std::atomic<int> received_count{0};
    std::vector<std::atomic<int>> per_producer_count(kProducers);
    for (auto& a : per_producer_count)
        a.store(0, std::memory_order_relaxed);

    // The multiplier exceeds kPerProducer, so dividing a payload by it
    // recovers the producer id unambiguously.
    auto encode = [](int prod_id, int seq) noexcept { return prod_id * 100000 + seq; };
    auto decode_producer = [](int payload) noexcept { return payload / 100000; };

    std::vector<std::jthread> producers;
    producers.reserve(kProducers);
    for (int prod_id = 0; prod_id < kProducers; ++prod_id) {
        producers.emplace_back([&ch, prod_id, encode](auto) {
            auto p_opt = ch.producer();
            if (!p_opt) return;
            auto prod_handle = std::move(*p_opt);
            auto psh = ses::mint_mpmc_producer_session<Channel>(::crucible::effects::HotFgCtx{}, prod_handle);
            for (int i = 0; i < kPerProducer; ++i) {
                auto next = std::move(psh).send(encode(prod_id, i), ses::blocking_push);
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
        });
    }

    std::vector<std::jthread> consumers;
    consumers.reserve(kConsumers);
    for (int cons_id = 0; cons_id < kConsumers; ++cons_id) {
        consumers.emplace_back([&ch, &received_count, &per_producer_count, decode_producer, kTotal](auto) {
            auto c_opt = ch.consumer();
            if (!c_opt) return;
            auto cons_handle = std::move(*c_opt);
            auto psh = ses::mint_mpmc_consumer_session<Channel>(::crucible::effects::HotFgCtx{}, cons_handle);

            // Consumers compete for items, so no consumer has a fixed
            // quota.  The loop bound is the global total for that reason.
            while (received_count.load(std::memory_order_acquire) < kTotal) {
                auto opt = cons_handle.try_pop();
                if (!opt) {
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                int prod_id = decode_producer(*opt);
                if (prod_id >= 0 && prod_id < kProducers) {
                    per_producer_count[static_cast<std::size_t>(prod_id)].fetch_add(1, std::memory_order_relaxed);
                }
                received_count.fetch_add(1, std::memory_order_acq_rel);
            }
            // The drain runs on the bare handle because this protocol
            // shape offers no exit branch, so a session recv() blocks
            // forever once the stream is empty.  Taking the alternative
            // would mean widening the protocol to Loop<Choice<Recv, Stop>>.
            std::move(psh).detach(TestInstrumentation{});
        });
    }

    for (auto& t : producers)
        t.join();
    for (auto& t : consumers)
        t.join();

    CRUCIBLE_TEST_REQUIRE(received_count.load(std::memory_order_acquire) == kTotal);
    for (int prod_id = 0; prod_id < kProducers; ++prod_id) {
        CRUCIBLE_TEST_REQUIRE(per_producer_count[static_cast<std::size_t>(prod_id)].load(std::memory_order_relaxed)
                              == kPerProducer);
    }
}

void test_typed_session_immediate_detach() {
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    Channel ch;

    auto p_opt = ch.producer();
    auto c_opt = ch.consumer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());
    CRUCIBLE_TEST_REQUIRE(c_opt.has_value());

    auto prod_handle = std::move(*p_opt);
    auto cons_handle = std::move(*c_opt);

    auto prod_psh = ses::mint_mpmc_producer_session<Channel>(::crucible::effects::HotFgCtx{}, prod_handle);
    auto cons_psh = ses::mint_mpmc_consumer_session<Channel>(::crucible::effects::HotFgCtx{}, cons_handle);

    std::move(prod_psh).detach(TestInstrumentation{});
    std::move(cons_psh).detach(TestInstrumentation{});

    // Reaching this point without an abort is the claim: both detach
    // calls were well-formed and the abandonment tracker stayed quiet.
    CRUCIBLE_TEST_REQUIRE(true);
}

void test_endpoint_helpers_are_forwarders() {
    Channel ch;

    auto via_helper = ses::mint_mpmc_producer_endpoint(ch);
    auto via_method = ch.producer();

    CRUCIBLE_TEST_REQUIRE(via_helper.has_value());
    CRUCIBLE_TEST_REQUIRE(via_method.has_value());

    // The helper and the method each drew their own share of the pool.
    CRUCIBLE_TEST_REQUIRE(ch.outstanding_producers() == 2);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_mpmc_channel_session]\n");
    run_test("typed_session_single_round_trip", test_typed_session_single_round_trip);
    run_test("typed_session_multi_round_trip", test_typed_session_multi_round_trip);
    run_test("typed_session_immediate_detach", test_typed_session_immediate_detach);
    run_test("endpoint_helpers_are_forwarders", test_endpoint_helpers_are_forwarders);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
