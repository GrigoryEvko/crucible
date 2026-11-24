// A crash session sends a marker through a write that only reads it through a
// reference and says that it took it.  The marker and its token would stay in
// the handle and end with no consume, and no crash would give them back.  A
// crash session takes only a trying write, which takes a marker by move and
// gives it back while it does not write it.

#include <fixy/Ctx.h>
#include <fixy/session/CrashTransport.h>

#include <cstddef>
#include <utility>

namespace s = fixy::session;
namespace fp = foundation::permissions;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_marker_write_only_reads_types {
struct Alice {};
struct Bob {};
struct Wire {
    int value = 0;
};
struct Region {
    using permission_row = foundation::effects::Row<>;
};
// The one root site of Region, so the protocol names the brand of its roots.
[[nodiscard]] inline auto mint_region_root() noexcept { return fp::mint_permission_root<Region>(); }
using RegionBrand = foundation::brand::brand_of_t<decltype(mint_region_root())>;
using Token = s::Transferable<int, Region, RegionBrand>;
using Relay = s::Recv<Token, s::Select<s::Send<Token, s::End>>>;
}  // namespace neg_sess_crash_marker_write_only_reads_types

using namespace neg_sess_crash_marker_write_only_reads_types;

int main() {
    s::CrashCell<Bob, Alice> cell;
    s::CrashCell<Alice, Bob> own;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto handle =
        s::mint_crash_session<Relay, Alice, Bob, s::ReliableSet<Bob>>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    auto [value, reply] = std::move(handle).recv([](Wire&) noexcept {
        auto [message, rest] = s::mint_permission_hold(mint_region_root()).template pack<Region>(1);
        static_cast<void>(std::move(rest).into_permissions());
        return ::fixy::some(std::move(message));
    });
    auto chosen = std::move(reply).template select<0>([](Wire&, std::size_t) noexcept { return true; });
    auto sent = std::move(chosen).send(std::move(value), [](Wire& wire, Token& token) noexcept -> bool {
        wire.value = token.peek();
        return true;
    });
    (void)std::move(sent.next).close();
    return 0;
}
