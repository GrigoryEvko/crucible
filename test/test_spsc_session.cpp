// The typed-session API driven end to end over the real channel primitive,
// rather than over a stand-in.
//
// What is in scope: round-trip integrity through the session API, size
// equality between a permissioned handle and the bare one under this target's
// build flags, and cross-thread use of the session type.
//
// What is deliberately out of scope: the permission set stays empty
// throughout, so nothing here exercises permission evolution across send and
// recv, branch convergence, or the balance check on a loop iteration. The
// streaming protocol has no choice point and the transports block
// unconditionally, so crash-driven shutdown is not reached either.

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/SpscSession.h>

namespace {

// A tag of its own, so the channel-tag tree cannot collide with another
// instantiation elsewhere.
struct TestChannelTag {};

using Channel = ::crucible::concurrent::PermissionedSpscChannel<int, 1024, TestChannelTag>;

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

// Both threads drive the session type rather than calling try_push and
// try_pop on the handles directly, which is the whole point of the exercise.

void test_typed_session_round_trip() {
    namespace ses = ::crucible::safety::proto::spsc_session;
    using ::crucible::safety::mint_permission_root;
    using ::crucible::safety::mint_permission_split;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    Channel ch;

    auto whole = mint_permission_root<Channel::whole_tag>();
    auto [pp, cp] = mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(whole));

    auto prod_handle = ch.producer(std::move(pp));
    auto cons_handle = ch.consumer(std::move(cp));

    constexpr int kCount = 1024;
    std::atomic<bool> producer_done{false};
    std::vector<int> received;
    received.reserve(kCount);

    std::jthread producer{[&prod_handle, &producer_done](auto) mutable {
        auto psh = ses::mint_producer_session<Channel>(::crucible::effects::HotFgCtx{}, prod_handle);
        for (int i = 0; i < kCount; ++i) {
            auto next = std::move(psh).send(i, ses::blocking_push);
            psh = std::move(next);
        }
        std::move(psh).detach(TestInstrumentation{});
        producer_done.store(true, std::memory_order_release);
    }};

    std::jthread consumer{[&cons_handle, &received](auto) mutable {
        auto psh = ses::mint_consumer_session<Channel>(::crucible::effects::HotFgCtx{}, cons_handle);
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

// A loop protocol has no exit branch, so a session on one is ended by an
// explicit detach rather than by close. Establishing a session and detaching
// it without moving any payload has to be well-formed, because that is the
// shape a caller wires up before any payload exists.

void test_typed_session_immediate_detach() {
    namespace ses = ::crucible::safety::proto::spsc_session;
    using ::crucible::safety::mint_permission_root;
    using ::crucible::safety::mint_permission_split;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    Channel ch;

    auto whole = mint_permission_root<Channel::whole_tag>();
    auto [pp, cp] = mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(whole));

    auto prod_handle = ch.producer(std::move(pp));
    auto cons_handle = ch.consumer(std::move(cp));

    auto prod_psh = ses::mint_producer_session<Channel>(::crucible::effects::HotFgCtx{}, prod_handle);
    auto cons_psh = ses::mint_consumer_session<Channel>(::crucible::effects::HotFgCtx{}, cons_handle);

    std::move(prod_psh).detach(TestInstrumentation{});
    std::move(cons_psh).detach(TestInstrumentation{});

    // The assertion is trivially true on purpose. Arriving here at all is the
    // claim: both detach calls are well-formed and the abandonment tracker
    // stays quiet.
    CRUCIBLE_TEST_REQUIRE(true);
}

// The header makes the same size claim, but against its own instantiations.
// Repeating it here under the real channel tag pins it to this target's build
// flags, where a layout drift would actually matter.
//
// The types named are the constructed heads rather than the loop. A loop is a
// shape-only template with no handle specialisation of its own, because it
// unrolls to its body's head when the session is minted.

namespace witness {
namespace proto = ::crucible::safety::proto;
using PSH_End_Prod = proto::PermissionedSessionHandle<proto::End, proto::EmptyPermSet, Channel::ProducerHandle*>;
using SH_End_Prod = proto::SessionHandle<proto::End, Channel::ProducerHandle*>;
static_assert(sizeof(PSH_End_Prod) == sizeof(SH_End_Prod),
              "a permissioned session handle over End must be the same size "
              "as the bare session handle over End.");

using PSH_End_Cons = proto::PermissionedSessionHandle<proto::End, proto::EmptyPermSet, Channel::ConsumerHandle*>;
using SH_End_Cons = proto::SessionHandle<proto::End, Channel::ConsumerHandle*>;
static_assert(sizeof(PSH_End_Cons) == sizeof(SH_End_Cons));
}  // namespace witness

}  // namespace

int main() {
    std::fprintf(stderr, "[test_spsc_session]\n");
    run_test("typed_session_round_trip", test_typed_session_round_trip);
    run_test("typed_session_immediate_detach", test_typed_session_immediate_detach);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
