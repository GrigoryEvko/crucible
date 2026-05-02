// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #9 — Multi-step body where MOST operations balance but
// the LAST step leaves PS imbalanced at Continue.
//
// THE BUG (looks REALLY innocent):
//   The user writes a "request-response-request" loop body:
//
//     Body: Recv<Transferable<int, X>,
//             Send<Returned<int, X>,
//               Recv<Transferable<int, Y>, Continue>>>
//
//   Iteration trace:
//     entry: {}
//     recv X:        {X}
//     send Ret X:    {}      (returned drains X — paired)
//     recv Y:        {Y}
//     Continue:      {Y} ≠ {} → FAILS
//
//   Looks like a balanced echo loop because the X half balances
//   (recv → send Ret).  But there's a TRAILING recv Y with no
//   surrender — the body recvs Y just before Continue and never
//   gives it back.  Subtle three-step bug hiding inside what looks
//   like a clean two-step echo pattern.
//
// WHY THE TYPE SYSTEM CATCHES IT (Decision D3):
//   The Loop balance assert at Continue checks the FINAL PS, not
//   the running balance.  Even though most of the body's
//   operations cancel out, the trailing recv Y leaves PS = {Y}
//   which differs from entry {}.
//
// WHY IT'S TRICKY:
//   Code review WOULD catch a one-step drain like fixtures #4/#5
//   easily.  But a body with several balanced sub-chains and ONE
//   trailing imbalance is much harder to spot — the eye sees the
//   recv-send pair and thinks "balanced", missing the trailing
//   half-pair.  The type system traces the FULL body sequentially
//   and reports the FINAL imbalance.

#include <crucible/sessions/PermissionedSession.h>

using namespace crucible::safety::proto;
using ::crucible::safety::Permission;
using ::crucible::safety::mint_permission_root;

namespace {
struct WorkX {};
struct WorkY {};
struct FakeChannel { int last_int = 0; };

Transferable<int, WorkX> wire_recv_x(FakeChannel& ch) noexcept {
    return Transferable<int, WorkX>{ch.last_int,
                                     mint_permission_root<WorkX>()};
}

Transferable<int, WorkY> wire_recv_y(FakeChannel& ch) noexcept {
    return Transferable<int, WorkY>{ch.last_int,
                                     mint_permission_root<WorkY>()};
}

void wire_send_ret_x(FakeChannel& ch, Returned<int, WorkX>&& r) noexcept {
    ch.last_int = r.value;
}

using BodyProto = Recv<Transferable<int, WorkX>,
                       Send<Returned<int, WorkX>,
                            Recv<Transferable<int, WorkY>, Continue>>>;
using LoopProto = Loop<BodyProto>;
}

int main() {
    auto h = mint_permissioned_session<LoopProto>(FakeChannel{});

    // Step 1: recv X — PS becomes {X}.
    auto [val_x, h2] = std::move(h).recv(wire_recv_x);

    // Step 2: send Returned<X> — PS becomes {} (paired with recv).
    auto h3 = std::move(h2).send(
        Returned<int, WorkX>{val_x.value, std::move(val_x.perm)},
        wire_send_ret_x);

    // Step 3: recv Y — PS becomes {Y}.  The next step is Continue,
    // which fires the balance assert because PS ({Y}) ≠ entry ({}).
    [[maybe_unused]] auto [val_y, h4] = std::move(h3).recv(wire_recv_y);
    return 0;
}
