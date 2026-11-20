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
// ids, and a stable id refuses a type with internal linkage.  The recorder
// folds the stable id of each payload too, and a stable id refuses the
// closure type of a fresh brand, so the token is a singleton root.
namespace neg_sess_crash_recorded_token_send_without_refusal_types {
struct Alice {};
struct Bob {};
struct Wire {};
struct Region {
    using permission_row = foundation::effects::Row<>;
};
[[nodiscard]] inline auto mint_region_root() noexcept { return fp::mint_singleton_root<Region>(); }
using RegionBrand = foundation::brand::brand_of_t<decltype(mint_region_root())>;
using Token = s::Transferable<int, Region, RegionBrand>;
using Relay = s::Recv<Token, s::Select<s::Send<Token, s::End>>>;
}  // namespace neg_sess_crash_recorded_token_send_without_refusal_types

using namespace neg_sess_crash_recorded_token_send_without_refusal_types;

int main() {
    s::CrashCell<Bob, Alice> cell;
    s::CrashCell<Alice, Bob> own;
    s::SessionEventLog log;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_recorded_session(
        s::mint_crash_session<Relay, Alice, Bob, s::ReliableSet<Bob>>(ctx, Wire{}, cell, s::mint_crash_writer(own)),
        s::mint_session_event_writer(log), s::RoleTagId{1}, s::RoleTagId{2});
    auto [value, reply] = std::move(handle).recv([](Wire&) noexcept {
        auto [message, rest] = s::mint_permission_hold(mint_region_root()).template pack<Region>(1);
        static_cast<void>(std::move(rest).into_permissions());
        return ::fixy::some(std::move(message));
    });
    auto chosen = std::move(reply).template select<0>([](Wire&, std::size_t) noexcept { return true; });
    auto sent = std::move(chosen).send(std::move(value), [](Wire&, Token&&, s::watch::wait_scope&) noexcept {});
    (void)std::move(sent.next).close();
    return 0;
}
