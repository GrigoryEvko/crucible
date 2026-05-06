// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-081 fixture #2 — mint_endpoint<Deque, Thief> exposes only the
// steal/Recv side.  It must not surface try_send/try_push through the
// generic endpoint API.

#include <crucible/concurrent/Endpoint.h>

namespace cc = crucible::concurrent;

namespace {
struct Tag {};
using Deque = cc::PermissionedChaseLevDeque<int, 64, Tag>;
}

int main() {
    Deque deque;
    auto thief = deque.thief();
    auto endpoint = cc::mint_endpoint<Deque, cc::Direction::Thief>(
        ::crucible::effects::HotFgCtx{}, *thief);
    [[maybe_unused]] bool ok = endpoint.try_send(1);
    return ok ? 0 : 1;
}
