// The recorder outside a crash-aware session relays a token with a write
// that cannot refuse: a declared write.  The recorder hands the crash
// transport under it a write of the same shape, so the crash transport
// still refuses a write that cannot keep the token with the caller.

#include <fixy/session/Recording.h>

#include <cstddef>
#include <optional>
#include <utility>

namespace s = fixy::session;
namespace fp = foundation::permissions;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_recorded_token_send_without_refusal_types {
struct Alice {};
struct Bob {};
struct Wire {};
struct Region {
    using permission_row = foundation::effects::Row<>;
};
using Token = s::Transferable<int, Region>;
using Relay = s::Recv<Token, s::Select<s::Send<Token, s::End>>>;
}  // namespace neg_sess_crash_recorded_token_send_without_refusal_types

using namespace neg_sess_crash_recorded_token_send_without_refusal_types;

int main() {
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    s::SessionEventLog log;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_recorded_session(
        s::mint_crash_session<Relay, Alice, Bob, s::ReliableSet<Bob>>(ctx, Wire{}, cell, s::mint_crash_writer(own)), log,
        s::RoleTagId{1}, s::RoleTagId{2});
    auto [token, reply] = std::move(handle).recv(
        [](Wire&) noexcept -> std::optional<Token> { return Token{1, fp::mint_permission_root<Region>()}; });
    auto chosen = std::move(reply).template select<0>([](Wire&, std::size_t) noexcept { return true; });
    auto sent = std::move(chosen).send(std::move(token), [](Wire&, Token&&, s::watch::wait_scope&) noexcept {});
    (void)std::move(sent.next).close();
    return 0;
}
