// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_thief_session over a work-stealing deque whose item carries the
// background row refuses the foreground context.  The thief receives stolen
// items and their row, which the context does not admit.

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/effects/_Computation.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

namespace eff = ::crucible::effects;
namespace ses = ::crucible::safety::proto::chaselev_session;

namespace {
struct Tag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<BgInt, 64, Tag>;
}  // namespace

inline void mint_under_foreground(Deque::ThiefHandle& handle) {
    auto session = ses::mint_thief_session<Deque>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
