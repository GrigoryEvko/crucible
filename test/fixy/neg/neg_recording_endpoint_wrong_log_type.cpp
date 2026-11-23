// Negative compile test.  This file must fail to compile.
//
// The recorder writes each step to a session event log through the one
// writer of the log.  An int is not a writer, so mint_recording_endpoint
// does not accept it.  The endpoint runs over the frame handle of the
// producer, so the call stands in the body of the frame door.
//
// Expected diagnostic: no overload of mint_recording_endpoint takes an int
// as the writer.

#include <fixy/channels/EndpointMint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace recording_log_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace recording_log_fixture

int main() {
    using namespace recording_log_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    namespace s = ::fixy::session;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept {
        int not_a_log = 0;
        auto recorded = channels::mint_recording_endpoint(
            channels::mint_endpoint<Channel, channels::Direction::Producer>(ctx, std::move(producer)), not_a_log,
            s::RoleTagId{1}, s::RoleTagId{2});
        (void)recorded;
    });
    return 0;
}
