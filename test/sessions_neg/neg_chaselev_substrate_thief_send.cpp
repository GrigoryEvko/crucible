// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-081 fixture #1 — mint_substrate_session<Deque, Thief> resolves
// to the Chase-Lev thief Recv-only protocol.  A thief session cannot
// send work into the owner side.

#include <crucible/concurrent/SubstrateSessionBridge.h>

#include <utility>

namespace cc = crucible::concurrent;

namespace {
struct Tag {};
using Deque = cc::PermissionedChaseLevDeque<int, 64, Tag>;
}

int main() {
    Deque deque;
    auto thief = deque.thief();
    auto psh = cc::mint_substrate_session<Deque, cc::Direction::Thief>(
        ::crucible::effects::HotFgCtx{}, *thief);
    [[maybe_unused]] auto next =
        std::move(psh).send(1, ::crucible::safety::proto::chaselev_session::blocking_owner_push);
    return 0;
}
