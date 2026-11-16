// An endpoint of the consumer of an MPSC channel pops a merge, so the
// context of the endpoint holds Nondet.  The foreground context holds no
// atom, and the mint of the endpoint refuses it.  The endpoint would
// otherwise give its pop the tag of a context that cannot hold it.

#include <fixy/channels/Endpoint.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>

namespace eff = foundation::effects;

namespace {

struct UserTag {};
struct UserBrand {};

using Channel = fixy::concurrent::PermissionedMpscChannel<int, 8, UserTag, UserBrand>;
using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

[[maybe_unused]] void attempt(FgCtx const& ctx, Channel::ConsumerHandle&& consumer) {
    [[maybe_unused]] auto endpoint =
        fixy::channels::mint_endpoint<Channel, fixy::channels::Direction::Consumer>(ctx, std::move(consumer));
}

}  // namespace

int main() { return 0; }
