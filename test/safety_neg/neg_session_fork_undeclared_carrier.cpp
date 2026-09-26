// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// session_fork operates the projection of G for each role on one shared
// channel.  A projection is safe only on per-pair FIFO.  A channel
// without a session_network value does not show that a projection is
// safe on it, and session_fork rejects it.
//
// Error: the shared channel has no session_network member.  G is well
// formed, the split manifest is declared, the channel is Pinned and the
// context is a real fork context.  Only the network gate rejects the
// call.
//
// Expected diagnostic: the static assertion Carrier_Not_Per_Pair_Fifo in
// session_fork.

#include <crucible/permissions/_Permission.h>
#include <crucible/safety/_Pinned.h>
#include <crucible/sessions/_PermissionedSession.h>
#include <crucible/sessions/_SessionGlobal.h>

#include <type_traits>
#include <utility>

namespace proto = ::crucible::safety::proto;

namespace neg_fork_undeclared {

struct ClientRole {};
struct ServerRole {};
struct Whole {};

struct UndeclaredChan : ::crucible::safety::Pinned<UndeclaredChan> {
    int request = 0;
    bool reply = false;
};

inline void client_send(UndeclaredChan& ch, int value) noexcept { ch.request = value; }
inline bool client_recv(UndeclaredChan& ch) noexcept { return ch.reply; }
inline int server_recv(UndeclaredChan& ch) noexcept { return ch.request; }
inline void server_send(UndeclaredChan& ch, bool value) noexcept { ch.reply = value; }

}  // namespace neg_fork_undeclared

namespace crucible::safety {
template <>
struct splits_into_pack<neg_fork_undeclared::Whole, neg_fork_undeclared::ClientRole,
                        neg_fork_undeclared::ServerRole> : std::true_type {};

template <>
struct splits_into_pack_authoring_witness<neg_fork_undeclared::Whole, neg_fork_undeclared::ClientRole,
                                          neg_fork_undeclared::ServerRole> : std::true_type {};
}  // namespace crucible::safety

int main() {
    using G = proto::Transmission<neg_fork_undeclared::ClientRole, neg_fork_undeclared::ServerRole, int,
                                  proto::Transmission<neg_fork_undeclared::ServerRole,
                                                      neg_fork_undeclared::ClientRole, bool, proto::End_G>>;

    neg_fork_undeclared::UndeclaredChan ch;
    auto whole = ::crucible::safety::mint_permission_root<neg_fork_undeclared::Whole>();

    auto rebuilt = proto::session_fork<G, neg_fork_undeclared::Whole, neg_fork_undeclared::ClientRole,
                                       neg_fork_undeclared::ServerRole>(
        ::crucible::safety::PermissionForkSpawnCtx{::crucible::effects::testing::bg()}, ch, std::move(whole),
        [](auto h_client) noexcept {
            auto h2 = std::move(h_client).send(7, neg_fork_undeclared::client_send);
            auto [reply, h3] = std::move(h2).recv(neg_fork_undeclared::client_recv);
            (void)reply;
            std::move(h3).detach(proto::detach_reason::TestInstrumentation{});
        },
        [](auto h_server) noexcept {
            auto [req, h2] = std::move(h_server).recv(neg_fork_undeclared::server_recv);
            auto h3 = std::move(h2).send(req == 7, neg_fork_undeclared::server_send);
            std::move(h3).detach(proto::detach_reason::TestInstrumentation{});
        });
    (void)rebuilt;
    return 0;
}
