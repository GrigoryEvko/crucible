// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SEPLOG-H2 fixture #1 — ConsumerProto is Recv-only.  Calling .send()
// on a ConsumerSession PSH (whose protocol is Loop<Recv<T, Continue>>)
// is a compile error: PSH only exposes send() on Send-headed protocols.
// Mirrors the chainedge_waiter_session_send fixture pattern; the
// MpmcChannelSession version pins the same role-discrimination claim
// for the fractional × fractional cell of the channel-permission family.

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/MpmcChannelSession.h>

#include <utility>

namespace conc = ::crucible::concurrent;
namespace ses  = ::crucible::safety::proto::mpmc_channel_session;

namespace {
struct Tag {};
using Channel = conc::PermissionedMpmcChannel<int, 64, Tag>;
}

int main() {
    Channel ch;
    auto c_opt = ch.consumer();
    if (!c_opt) return 1;
    auto cons_handle = std::move(*c_opt);
    auto psh = ses::mint_mpmc_consumer_session<Channel>(
        ::crucible::effects::HotFgCtx{}, cons_handle);
    [[maybe_unused]] auto bad =
        std::move(psh).send(42, ses::blocking_push);
    return 0;
}
