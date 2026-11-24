// A borrowing body that holds a live Linear<T>, given to the producer door of
// a merge channel.  The door gives no borrowing body back, so on a refused
// lend it would end the body in its own frame, and the Linear would end with
// its obligation open.  A Linear owes a consume at its end, so the door
// refuses the body (LendDoorBody of fixy/concurrent/HandleTraits.h).

#include <fixy/Qtt.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct MergeTag {};
auto merge_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, MergeTag>>();
}
}  // namespace

using Channel = c::channel_t<int, 8, decltype(merge_root())>;

int main() {
    auto [producer_root, consumer] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(merge_root());
    Channel channel{std::move(producer_root)};
    int seen = 0;
    auto body = [&seen, held = fixy::mint_linear<int>(1)](auto /*producer*/) mutable noexcept {
        seen = std::move(held).consume();
    };
    static_cast<void>(c::with_producer(channel, ::foundation::effects::testing::test().nondet, std::move(body)));
    ::foundation::linear::drop(std::move(consumer));
    return seen;
}
