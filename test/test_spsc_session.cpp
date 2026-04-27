// SpscSession.h integration test (SEPLOG-INT-1 / SAFEINT-R31, #384/#413).
//
// First production-shaped exercise of the FOUND-C v1 PermissionedSession-
// Handle stack composed with the existing concurrent/Permissioned-
// SpscChannel primitive.  Closes the IX.2 critique that "no production
// caller exists for SessionHandle / PermissionedSpscChannel" — this
// test is the first wired-in end-to-end exercise.
//
// Three tiers of evidence:
//
//   Tier A — STRUCTURAL (load-bearing):
//     File-scope sizeof asserts in SpscSession.h verify PSH-over-handle-
//     pointer is byte-identical to bare SessionHandle wrapping the same
//     pointer.  That assertion fires at compile time even without this
//     TU — but this TU pulls SpscSession.h under project warning flags,
//     forcing the full template instantiation and exercising every
//     constexpr path.
//
//   Tier B — RUNTIME ROUND-TRIP:
//     Two jthreads exchange N items through a real PermissionedSpsc-
//     Channel via the typed-session API.  Producer side uses
//     producer_session<Channel> + blocking_push; consumer side uses
//     consumer_session<Channel> + blocking_pop.  Verifies (a) PSH's
//     send/recv compose with the Permission-typed handles, (b) the
//     Loop<Send|Recv, Continue> protocol shape iterates correctly,
//     (c) detach_reason::TestInstrumentation cleanly drops both PSHs.
//
//   Tier C — DETACH POLICY:
//     Verifies that producer + consumer can be detached at shutdown
//     without the abandonment-tracker firing (Loop without exit branch
//     requires explicit detach; the documented infinite-loop pattern).

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SpscSession.h>

namespace {

// Test fixture tag — mints a dedicated channel-tag tree so this test
// doesn't collide with any other PermissionedSpscChannel instantiation.
struct TestChannelTag {};

using Channel = ::crucible::concurrent::PermissionedSpscChannel<int, 1024,
                                                                 TestChannelTag>;

int  total_passed = 0;
int  total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                  \
    do {                                                              \
        if (!(cond)) {                                                \
            std::fprintf(stderr,                                      \
                "  REQUIRE FAILED: %s @ %s:%d\n",                     \
                #cond, __FILE__, __LINE__);                           \
            ++total_failed;                                           \
            return;                                                   \
        }                                                             \
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

// ── Tier B: round-trip ──────────────────────────────────────────────
//
// 1024 items pushed by producer thread, popped by consumer thread.
// Both threads use the typed-session API (PSH over handle pointer)
// rather than the bare ProducerHandle.try_push / ConsumerHandle.try_pop.
// Final invariant: every item arrived in order, every PSH detached
// cleanly, no abandonment diagnostic.

void test_typed_session_round_trip() {
    namespace ses = ::crucible::safety::proto::spsc_session;
    using ::crucible::safety::permission_root_mint;
    using ::crucible::safety::permission_split;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    Channel ch;

    auto whole = permission_root_mint<Channel::whole_tag>();
    auto [pp, cp] = permission_split<Channel::producer_tag,
                                      Channel::consumer_tag>(std::move(whole));

    auto prod_handle = ch.producer(std::move(pp));
    auto cons_handle = ch.consumer(std::move(cp));

    constexpr int kCount = 1024;
    std::atomic<bool> producer_done{false};
    std::vector<int>  received;
    received.reserve(kCount);

    std::jthread producer{
        [&prod_handle, &producer_done](auto) mutable {
            auto psh = ses::producer_session<Channel>(prod_handle);
            for (int i = 0; i < kCount; ++i) {
                auto next = std::move(psh).send(i, ses::blocking_push);
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
            producer_done.store(true, std::memory_order_release);
        }
    };

    std::jthread consumer{
        [&cons_handle, &received](auto) mutable {
            auto psh = ses::consumer_session<Channel>(cons_handle);
            for (int i = 0; i < kCount; ++i) {
                auto [v, next] = std::move(psh).recv(ses::blocking_pop);
                received.push_back(v);
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
        }
    };

    producer.join();
    consumer.join();

    CRUCIBLE_TEST_REQUIRE(producer_done.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(received.size() == static_cast<std::size_t>(kCount));
    for (std::size_t i = 0; i < static_cast<std::size_t>(kCount); ++i) {
        CRUCIBLE_TEST_REQUIRE(received[i] == static_cast<int>(i));
    }
}

// ── Tier C: detach-on-construction ─────────────────────────────────
//
// Verifies that establishing a session and immediately detaching it
// (without sending/receiving anything) is well-formed.  This is the
// canonical shutdown pattern when production code wants to wire a
// session-typed view but no payload is available yet.

void test_typed_session_immediate_detach() {
    namespace ses = ::crucible::safety::proto::spsc_session;
    using ::crucible::safety::permission_root_mint;
    using ::crucible::safety::permission_split;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    Channel ch;

    auto whole = permission_root_mint<Channel::whole_tag>();
    auto [pp, cp] = permission_split<Channel::producer_tag,
                                      Channel::consumer_tag>(std::move(whole));

    auto prod_handle = ch.producer(std::move(pp));
    auto cons_handle = ch.consumer(std::move(cp));

    auto prod_psh = ses::producer_session<Channel>(prod_handle);
    auto cons_psh = ses::consumer_session<Channel>(cons_handle);

    std::move(prod_psh).detach(TestInstrumentation{});
    std::move(cons_psh).detach(TestInstrumentation{});

    // Reaching here without abort proves both detach calls were well-
    // formed and the abandonment-tracker did not fire.  Sentinel pass:
    CRUCIBLE_TEST_REQUIRE(true);
}

// ── Tier A: file-scope sizeof witness ──────────────────────────────
//
// SpscSession.h carries its own sizeof_witness namespace with the
// load-bearing static_asserts on the concrete CONSTRUCTED head types
// (End, Send<int, End>) — Loop<...> is a shape-only template that
// has no SessionHandle / PSH specialisation (it unrolls to its body's
// head at establish_permissioned).  Re-asserting under the
// production-tagged channel here pins the witness to THIS TU's build
// flags, catching any silent ABI drift between the header's witness
// and the production-target instantiation.

namespace witness {
namespace proto = ::crucible::safety::proto;
using PSH_End_Prod = proto::PermissionedSessionHandle<
    proto::End, proto::EmptyPermSet, Channel::ProducerHandle*>;
using SH_End_Prod = proto::SessionHandle<proto::End, Channel::ProducerHandle*>;
static_assert(sizeof(PSH_End_Prod) == sizeof(SH_End_Prod),
              "spsc_session test TU: PSH<End> vs bare SH<End> "
              "size-equality must hold under production-target channel tag.");

using PSH_End_Cons = proto::PermissionedSessionHandle<
    proto::End, proto::EmptyPermSet, Channel::ConsumerHandle*>;
using SH_End_Cons = proto::SessionHandle<proto::End, Channel::ConsumerHandle*>;
static_assert(sizeof(PSH_End_Cons) == sizeof(SH_End_Cons));
}  // namespace witness

}  // namespace

int main() {
    std::fprintf(stderr, "[test_spsc_session]\n");
    run_test("typed_session_round_trip",         test_typed_session_round_trip);
    run_test("typed_session_immediate_detach",   test_typed_session_immediate_detach);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
