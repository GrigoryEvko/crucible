// A write that only reads a marker through a reference, and says that it took
// the marker, leaves the marker and its token in the handle, where they end
// with no consume.  So the send refuses a marker write of the shape of a
// copyable payload, also when the write moves nothing.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Brand.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_send_marker_write_only_reads_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_send_marker_write_only_reads_types

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
using neg_sess_send_marker_write_only_reads_types::Region;

struct Wire {
    int value = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Message = s::Transferable<int, Region, ::foundation::brand::brand_of_t<decltype(root)>>;
    auto sender = s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, Wire{}, std::move(root));
    auto sender_done = std::move(sender).send(1, [](Wire& wire, Message& message) noexcept -> bool {
        wire.value = message.peek();
        return true;
    });
    static_cast<void>(std::move(sender_done).close());
    return 0;
}
