// Negative compile test.  This file must fail to compile.
//
// A unit gives the producer end of an SPSC channel a protocol of its own, so
// the two ends of the channel would run protocols that are not duals.  The
// protocol of the consumer end is the dual of the protocol of the producer
// end, and the two are aliases.  No unit can specialize an alias, so no unit
// can give one end a protocol that the other end does not face.
//
// Expected diagnostic: default_proto_for_t is an alias, which no unit can
// specialize.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/permissions/Permission.h>

namespace substrate_alias_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace substrate_alias_fixture

template <>
struct fixy::channels::default_proto_for_t<substrate_alias_fixture::Channel, ::fixy::channels::Direction::Producer> {
    using type = ::fixy::session::Loop<::fixy::session::Send<long, ::fixy::session::Continue>>;
};

int main() { return 0; }
