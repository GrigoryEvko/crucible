// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SEPLOG-H2 fixture #2 — ProducerProto is Send-only.  Calling .recv()
// on a ProducerSession PSH (whose protocol is Loop<Send<T, Continue>>)
// is a compile error: PSH only exposes recv() on Recv-headed protocols.
// Symmetric to fixture #1; together they pin the bidirectional
// role-discrimination claim at the typed-session layer.

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
    auto p_opt = ch.producer();
    if (!p_opt) return 1;
    auto prod_handle = std::move(*p_opt);
    auto psh = ses::mint_mpmc_producer_session<Channel>(
        ::crucible::effects::HotFgCtx{}, prod_handle);
    [[maybe_unused]] auto bad =
        std::move(psh).recv(ses::blocking_pop);
    return 0;
}
