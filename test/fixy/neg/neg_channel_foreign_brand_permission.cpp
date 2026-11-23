// A channel has the brand of the root site that its permissions grow
// from.  Two root sites of one user tag give two brands, so the two
// channels here have one value type, one capacity and one tag, and they
// are still two types.  A producer token of the second site does not
// open the channel of the first site at its frame door.

#include <fixy/concurrent/PermissionedChannel.h>

#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {
struct TraceTag {};
auto first_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, TraceTag>>();
}
auto second_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, TraceTag>>();
}
}  // namespace

using FirstChannel = c::channel_t<int, 8, decltype(first_root())>;
using SecondChannel = c::channel_t<int, 8, decltype(second_root())>;

int main() {
    FirstChannel first{};
    auto second_tokens =
        perm::mint_permission_split<SecondChannel::producer_tag, SecondChannel::consumer_tag>(second_root());

    auto wrong = c::with_producer(first, std::move(second_tokens.at(::foundation::core::index<0>())),
                                  [](auto producer) noexcept {
                                      [[maybe_unused]] bool const is_pushed = producer.try_push(1);
                                      return std::move(producer).release();
                                  });
    return 0;
}
