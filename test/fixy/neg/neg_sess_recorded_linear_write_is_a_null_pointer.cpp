// A pointer to a function can be null, and a send that calls a null pointer
// ends in undefined behavior with the marker in its hand.  The pointer has
// the exact shape of the write of a marker.  The recorded send refuses it,
// because the write of a linear payload is a class with a call operator.

#include <fixy/Ctx.h>
#include <fixy/session/Recording.h>

#include <utility>

namespace s = fixy::session;
namespace fp = foundation::permissions;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.  The recorder
// folds the stable id of each payload too, and a stable id refuses the
// closure type of a fresh brand, so the token is a singleton root.
namespace neg_sess_recorded_linear_write_is_a_null_pointer_types {
struct Alice {};
struct Bob {};
struct Wire {};
struct Region {
    using permission_row = foundation::effects::Row<>;
};
[[nodiscard]] inline auto mint_region_root() noexcept { return fp::mint_singleton_root<Region>(); }
using Message = s::Transferable<int, Region, foundation::brand::brand_of_t<decltype(mint_region_root())>>;
using Sends = s::Send<Message, s::End>;
[[nodiscard]] inline ::fixy::Option<Message> put_message(Wire&, Message&& message) noexcept {
    return ::fixy::Option<Message>::some(std::move(message));
}
}  // namespace neg_sess_recorded_linear_write_is_a_null_pointer_types

using namespace neg_sess_recorded_linear_write_is_a_null_pointer_types;

int main() {
    s::SessionEventLog log;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_recorded_session(s::mint_permissioned_session<Sends>(ctx, Wire{}, mint_region_root()),
                                           s::mint_session_event_writer(log), s::RoleTagId{1}, s::RoleTagId{2});
    auto at_end = std::move(handle).send(1, decltype(&put_message){});
    (void)std::move(at_end).close();
    return 0;
}
