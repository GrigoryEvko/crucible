// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-060 fixture #1 — the thief session protocol is Recv-only.
// A thief may steal borrowed work; it cannot push work into the owner
// side of the Chase-Lev deque.

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

#include <utility>

namespace concur = crucible::concurrent;
namespace ses = crucible::safety::proto::chaselev_session;

namespace {
struct Tag {};
using Deque = concur::PermissionedChaseLevDeque<int, 64, Tag>;
}

int main() {
    Deque deque;
    auto thief = ses::mint_chaselev_thief<Deque>(deque);
    auto psh = ses::mint_thief_session<Deque>(*thief);
    [[maybe_unused]] auto next =
        std::move(psh).send(1, ses::blocking_owner_push);
    return 0;
}
