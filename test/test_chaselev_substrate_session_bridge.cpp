// GAPS-081: SubstrateSessionBridge support for PermissionedChaseLevDeque.
//
// Exercises the generic substrate bridge and Endpoint surface over the
// existing ChaseLev owner/thief session protocols.  The owner endpoint is
// push+pop capable; the thief endpoint is steal/Recv-only.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/permissions/Permission.h>

#include <cassert>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

namespace cc = ::crucible::concurrent;
namespace proto = ::crucible::safety::proto;
namespace safety = ::crucible::safety;
namespace ses = ::crucible::safety::proto::chaselev_session;

struct BridgeTag {};
using Deque = cc::PermissionedChaseLevDeque<int, 128, BridgeTag>;

static_assert(cc::IsBridgeableDirection<Deque, cc::Direction::Owner>);
static_assert(cc::IsBridgeableDirection<Deque, cc::Direction::Thief>);
static_assert(!cc::IsBridgeableDirection<Deque, cc::Direction::Producer>);
static_assert(!cc::IsBridgeableDirection<Deque, cc::Direction::Consumer>);

static_assert(std::is_same_v<cc::handle_for_t<Deque, cc::Direction::Owner>,
                             Deque::OwnerHandle>);
static_assert(std::is_same_v<cc::handle_for_t<Deque, cc::Direction::Thief>,
                             Deque::ThiefHandle>);
static_assert(std::is_same_v<cc::default_proto_for_t<Deque, cc::Direction::Owner>,
                             ses::OwnerProto<int>>);
static_assert(std::is_same_v<cc::default_proto_for_t<Deque, cc::Direction::Thief>,
                             ses::ThiefProto<int, Deque::thief_tag>>);

using OwnerEndpoint = decltype(cc::mint_endpoint<Deque, cc::Direction::Owner>(
    std::declval<::crucible::effects::HotFgCtx const&>(),
    std::declval<Deque::OwnerHandle&>()));
using ThiefEndpoint = decltype(cc::mint_endpoint<Deque, cc::Direction::Thief>(
    std::declval<::crucible::effects::HotFgCtx const&>(),
    std::declval<Deque::ThiefHandle&>()));

template <typename Ep>
concept EndpointCanSend = requires(Ep& ep) {
    ep.try_send(1);
};

template <typename Ep>
concept EndpointCanRecv = requires(Ep& ep) {
    ep.try_recv();
};

static_assert(EndpointCanSend<OwnerEndpoint>);
static_assert(EndpointCanRecv<OwnerEndpoint>);
static_assert(!EndpointCanSend<ThiefEndpoint>);
static_assert(EndpointCanRecv<ThiefEndpoint>);

int test_substrate_sessions_owner_and_thief() {
    using proto::detach_reason::TestInstrumentation;

    Deque deque;
    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = deque.owner(std::move(owner_perm));

    auto owner_session = cc::mint_substrate_session<Deque, cc::Direction::Owner>(
        ::crucible::effects::HotFgCtx{}, owner);

    auto push_10 = std::move(owner_session).select_local<ses::owner_push_branch>();
    owner_session = std::move(push_10).send(10, ses::blocking_owner_push);
    auto push_20 = std::move(owner_session).select_local<ses::owner_push_branch>();
    owner_session = std::move(push_20).send(20, ses::blocking_owner_push);

    auto thief_opt = deque.thief();
    assert(thief_opt.has_value());
    auto thief_session = cc::mint_substrate_session<Deque, cc::Direction::Thief>(
        ::crucible::effects::HotFgCtx{}, *thief_opt);

    auto [borrowed, thief_next] =
        std::move(thief_session).recv(ses::blocking_steal_borrowed);
    assert(borrowed.value == 10);

    auto pop_owner = std::move(owner_session).select_local<ses::owner_pop_branch>();
    auto [popped, owner_next] =
        std::move(pop_owner).recv(ses::blocking_owner_pop);
    assert(popped == 20);

    std::move(owner_next).detach(TestInstrumentation{});
    std::move(thief_next).detach(TestInstrumentation{});
    assert(deque.empty_approx());
    return 0;
}

int test_endpoint_owner_and_thief_raw_views() {
    Deque deque;
    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = deque.owner(std::move(owner_perm));

    auto owner_ep = cc::mint_endpoint<Deque, cc::Direction::Owner>(
        ::crucible::effects::HotFgCtx{}, owner);
    assert(owner_ep.try_send(1));
    assert(owner_ep.try_send(2));
    auto owner_pop = owner_ep.try_recv();
    assert(owner_pop.has_value());
    assert(*owner_pop == 2);

    auto thief_opt = deque.thief();
    assert(thief_opt.has_value());
    auto thief_ep = cc::mint_endpoint<Deque, cc::Direction::Thief>(
        ::crucible::effects::HotFgCtx{}, *thief_opt);
    auto stolen = thief_ep.try_recv();
    assert(stolen.has_value());
    assert(*stolen == 1);
    assert(deque.empty_approx());
    return 0;
}

int test_endpoint_into_session() {
    using proto::detach_reason::TestInstrumentation;

    Deque deque;
    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = deque.owner(std::move(owner_perm));

    auto owner_ep = cc::mint_endpoint<Deque, cc::Direction::Owner>(
        ::crucible::effects::HotFgCtx{}, owner);
    auto owner_session = std::move(owner_ep).into_session();
    auto push = std::move(owner_session).select_local<ses::owner_push_branch>();
    auto next = std::move(push).send(33, ses::blocking_owner_push);
    std::move(next).detach(TestInstrumentation{});
    auto popped = owner.try_pop();
    assert(popped.has_value());
    assert(*popped == 33);
    assert(deque.empty_approx());
    return 0;
}

}  // namespace

int main() {
    if (int rc = test_substrate_sessions_owner_and_thief(); rc != 0) return rc;
    if (int rc = test_endpoint_owner_and_thief_raw_views(); rc != 0) return 10 + rc;
    if (int rc = test_endpoint_into_session(); rc != 0) return 20 + rc;
    std::puts("chaselev_substrate_session_bridge: OK");
    return 0;
}
