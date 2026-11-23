// The body of a frame door of a merge gives its producer handle back as the
// result, so the handle and its share of the pool would leave the frame.  The
// gate of the door refuses a result that names the scope brand of the frame
// (FrameResult of foundation/core/Choice.h).

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct LogTag {};
auto log_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, LogTag>>(); }
}  // namespace

using Channel = c::channel_t<int, 8, decltype(log_root())>;

int main() {
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(log_root());
    Channel channel{std::move(tokens.at(::foundation::core::index<0>()))};
    auto escaped = c::with_producer(channel, ::foundation::effects::testing::test().nondet,
                                    [](auto producer) noexcept { return producer; });
    return escaped.is_some() ? 0 : 1;
}
