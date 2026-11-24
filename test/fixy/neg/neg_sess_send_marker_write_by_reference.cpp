// The write of a marker takes the marker by move and gives it back while it
// has no room.  This write has the shape of a copyable payload: it moves the
// marker out of a reference, and the handle keeps a marker that looks live.
// So the send refuses the write.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Brand.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_send_marker_write_by_reference_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_send_marker_write_by_reference_types

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
using neg_sess_send_marker_write_by_reference_types::Region;

template <class Message>
struct Mailbox {
    ::fixy::FixedList<Message, 1> held{};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Message = s::Transferable<int, Region, ::foundation::brand::brand_of_t<decltype(root)>>;
    auto sender = s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, Mailbox<Message>{}, std::move(root));
    auto sender_done = std::move(sender).send(1, [](Mailbox<Message>& box, Message& message) noexcept -> bool {
        static_cast<void>(box.held.append(std::move(message)).expect("empty"));
        return true;
    });
    static_cast<void>(std::move(sender_done).close());
    return 0;
}
