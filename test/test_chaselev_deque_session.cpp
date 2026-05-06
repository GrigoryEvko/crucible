// ChaseLevDequeSession.h integration test (GAPS-060).
//
// Exercises the typed-session facade over PermissionedChaseLevDeque:
// owner branch selection for push/pop, thief Borrowed recv, SharedPermission
// proof admission, pointer-resource sizeof witnesses, and multi-thief
// fixed-work stealing through PermissionedSessionHandle.

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

namespace safety = ::crucible::safety;
namespace proto = ::crucible::safety::proto;
namespace ses = ::crucible::safety::proto::chaselev_session;
namespace concur = ::crucible::concurrent;

struct TestTag {};
struct MultiTag {};

using Deque = concur::PermissionedChaseLevDeque<int, 512, TestTag>;

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_REQUIRE(cond)                                             \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n",        \
                         #cond, __FILE__, __LINE__);                      \
            ++total_failed;                                                \
            return;                                                        \
        }                                                                  \
    } while (0)

template <typename Body>
void run_test(char const* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int const before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

static_assert(ses::ChaseLevSessionSurface<Deque>);
static_assert(std::is_same_v<
    ses::OwnerProto<int>,
    proto::Loop<proto::Select<proto::Send<int, proto::Continue>,
                              proto::Recv<int, proto::Continue>>>>);
static_assert(std::is_same_v<
    ses::ThiefProto<int, Deque::thief_tag>,
    proto::Loop<proto::Recv<proto::Borrowed<int, Deque::thief_tag>,
                            proto::Continue>>>);
static_assert(sizeof(proto::PermissionedSessionHandle<
                  proto::End, proto::EmptyPermSet, Deque::OwnerHandle*>)
              == sizeof(proto::SessionHandle<proto::End,
                                             Deque::OwnerHandle*>));
static_assert(sizeof(proto::PermissionedSessionHandle<
                  proto::End, proto::EmptyPermSet, Deque::ThiefHandle*>)
              == sizeof(proto::SessionHandle<proto::End,
                                             Deque::ThiefHandle*>));

template <typename H>
concept CanUseOwnerHotPush = requires(H& h) {
    ses::owner_session_try_push<Deque>(h, 1);
};

template <typename H>
concept CanUseThiefHotSteal = requires(H& h) {
    ses::thief_session_steal_borrowed<Deque>(h);
};

static_assert(!CanUseOwnerHotPush<ses::ThiefSessionHandle<Deque>>);
static_assert(!CanUseThiefHotSteal<ses::OwnerSessionHandle<Deque>>);

void test_mint_factories_and_thief_proof() {
    Deque deque;

    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = ses::mint_chaselev_owner<Deque>(
        deque, std::move(owner_perm));
    CRUCIBLE_REQUIRE(owner.try_push(7));

    auto first_thief = ses::mint_chaselev_thief<Deque>(deque);
    CRUCIBLE_REQUIRE(first_thief.has_value());
    auto proof = first_thief->token();

    auto second_thief = ses::mint_chaselev_thief<Deque>(deque, proof);
    CRUCIBLE_REQUIRE(second_thief.has_value());
    CRUCIBLE_REQUIRE(deque.outstanding_thieves() == 2);

    second_thief.reset();
    first_thief.reset();
    CRUCIBLE_REQUIRE(deque.outstanding_thieves() == 0);
}

void test_owner_session_push_pop() {
    using proto::detach_reason::TestInstrumentation;

    Deque deque;
    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = ses::mint_chaselev_owner<Deque>(
        deque, std::move(owner_perm));
    auto psh = ses::mint_owner_session<Deque>(owner);

    for (int i = 1; i <= 4; ++i) {
        auto push = std::move(psh).select_local<ses::owner_push_branch>();
        psh = std::move(push).send(i, ses::blocking_owner_push);
    }

    for (int expected = 4; expected >= 1; --expected) {
        auto pop = std::move(psh).select_local<ses::owner_pop_branch>();
        auto [value, next] = std::move(pop).recv(ses::blocking_owner_pop);
        CRUCIBLE_REQUIRE(value == expected);
        psh = std::move(next);
    }

    std::move(psh).detach(TestInstrumentation{});
    CRUCIBLE_REQUIRE(deque.empty_approx());
}

void test_thief_session_receives_borrowed_work() {
    using proto::detach_reason::TestInstrumentation;

    Deque deque;
    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = ses::mint_chaselev_owner<Deque>(
        deque, std::move(owner_perm));
    CRUCIBLE_REQUIRE(owner.try_push(10));
    CRUCIBLE_REQUIRE(owner.try_push(20));

    auto thief_opt = ses::mint_chaselev_thief<Deque>(deque);
    CRUCIBLE_REQUIRE(thief_opt.has_value());
    auto thief_psh = ses::mint_thief_session<Deque>(*thief_opt);

    auto [borrowed, next] =
        std::move(thief_psh).recv(ses::blocking_steal_borrowed);
    CRUCIBLE_REQUIRE(borrowed.value == 10);

    std::move(next).detach(TestInstrumentation{});
}

void test_fused_loop_hot_helpers_preserve_roles() {
    using proto::detach_reason::TestInstrumentation;

    Deque deque;
    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = ses::mint_chaselev_owner<Deque>(
        deque, std::move(owner_perm));
    auto owner_psh = ses::mint_owner_session<Deque>(owner);

    CRUCIBLE_REQUIRE(ses::owner_session_try_push<Deque>(owner_psh, 11));
    CRUCIBLE_REQUIRE(ses::owner_session_try_push<Deque>(owner_psh, 22));

    auto popped = ses::owner_session_try_pop<Deque>(owner_psh);
    CRUCIBLE_REQUIRE(popped.has_value());
    CRUCIBLE_REQUIRE(*popped == 22);

    auto thief_opt = ses::mint_chaselev_thief<Deque>(deque);
    CRUCIBLE_REQUIRE(thief_opt.has_value());
    auto thief_psh = ses::mint_thief_session<Deque>(*thief_opt);
    auto borrowed = ses::thief_session_steal_borrowed<Deque>(thief_psh);
    CRUCIBLE_REQUIRE(borrowed.value == 11);

    std::move(owner_psh).detach(TestInstrumentation{});
    std::move(thief_psh).detach(TestInstrumentation{});
}

void test_four_thief_sessions_steal_fixed_work() {
    using proto::detach_reason::TestInstrumentation;

    using MultiDeque = concur::PermissionedChaseLevDeque<int, 512, MultiTag>;
    constexpr int kThieves = 4;
    constexpr int kPerThief = 32;
    constexpr int kTotal = kThieves * kPerThief;

    MultiDeque deque;
    auto owner_perm = safety::mint_permission_root<MultiDeque::owner_tag>();
    auto owner = ses::mint_chaselev_owner<MultiDeque>(
        deque, std::move(owner_perm));
    for (int i = 1; i <= kTotal; ++i) {
        CRUCIBLE_REQUIRE(owner.try_push(i));
    }

    std::atomic<int> stolen_count{0};
    std::atomic<int> stolen_sum{0};
    std::atomic<bool> failed{false};
    std::array<std::thread, kThieves> thieves{};

    for (int idx = 0; idx < kThieves; ++idx) {
        thieves[static_cast<std::size_t>(idx)] = std::thread{[&] {
            auto thief_opt = ses::mint_chaselev_thief<MultiDeque>(deque);
            if (!thief_opt) {
                failed.store(true, std::memory_order_release);
                return;
            }
            auto psh = ses::mint_thief_session<MultiDeque>(*thief_opt);
            int local_sum = 0;
            for (int i = 0; i < kPerThief; ++i) {
                auto [borrowed, next] =
                    std::move(psh).recv(ses::blocking_steal_borrowed);
                local_sum += borrowed.value;
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
            stolen_sum.fetch_add(local_sum, std::memory_order_relaxed);
            stolen_count.fetch_add(kPerThief, std::memory_order_relaxed);
        }};
    }

    for (auto& thief : thieves) thief.join();

    constexpr int expected_sum = kTotal * (kTotal + 1) / 2;
    CRUCIBLE_REQUIRE(!failed.load(std::memory_order_acquire));
    CRUCIBLE_REQUIRE(stolen_count.load(std::memory_order_relaxed) == kTotal);
    CRUCIBLE_REQUIRE(stolen_sum.load(std::memory_order_relaxed) == expected_sum);
    CRUCIBLE_REQUIRE(deque.empty_approx());
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_chaselev_deque_session]\n");
    run_test("mint_factories_and_thief_proof",
             test_mint_factories_and_thief_proof);
    run_test("owner_session_push_pop",
             test_owner_session_push_pop);
    run_test("thief_session_receives_borrowed_work",
             test_thief_session_receives_borrowed_work);
    run_test("fused_loop_hot_helpers_preserve_roles",
             test_fused_loop_hot_helpers_preserve_roles);
    run_test("four_thief_sessions_steal_fixed_work",
             test_four_thief_sessions_steal_fixed_work);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
