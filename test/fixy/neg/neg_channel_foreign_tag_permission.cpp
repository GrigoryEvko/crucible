// Two channels of two user tags have two sets of Permission types.  A
// producer token of one channel does not open the other, so neither
// channel gets a second producer.
//
// The two channels here carry the same value type and the same capacity.
// The fixture splits the token correctly for its own channel.  The frame
// door that gives the token to the other channel fails.

#include <fixy/concurrent/PermissionedChannel.h>

#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {
struct TraceTag {};
struct MetaTag {};
auto trace_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, TraceTag>>();
}
auto meta_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, MetaTag>>(); }
}  // namespace

using TraceChannel = c::channel_t<int, 8, decltype(trace_root())>;
using MetaChannel = c::channel_t<int, 8, decltype(meta_root())>;

int main() {
    MetaChannel meta{};
    auto trace_tokens =
        perm::mint_permission_split<TraceChannel::producer_tag, TraceChannel::consumer_tag>(trace_root());

    auto wrong =
        c::with_producer(meta, std::move(trace_tokens.at(::foundation::core::index<0>())), [](auto producer) noexcept {
            [[maybe_unused]] bool const is_pushed = producer.try_push(1);
            return std::move(producer).release();
        });
    return 0;
}
