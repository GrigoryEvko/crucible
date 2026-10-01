// The compile-time checks of fixy/handle/LazyEstablishedChannel.h.

#include <fixy/handle/LazyEstablishedChannel.h>

namespace fixy::handle {

namespace detail::lazy_established_channel_self_test {

struct Wire : ::foundation::Pinned<Wire> {
    int sentinel = 0;
};

using Stream = ::fixy::session::Loop<
    ::fixy::session::Select<::fixy::session::Send<int, ::fixy::session::Continue>, ::fixy::session::End>>;
using Channel = LazyEstablishedChannel<Stream, Wire>;

static_assert(!std::is_copy_constructible_v<Channel>);
static_assert(!std::is_move_constructible_v<Channel>);
static_assert(std::is_base_of_v<::foundation::Pinned<Channel>, Channel>);
static_assert(sizeof(Channel) == 2 * sizeof(std::atomic<Wire*>),
              "the channel is the publication slot and the claim flag, and nothing more");

}  // namespace detail::lazy_established_channel_self_test

}  // namespace fixy::handle
