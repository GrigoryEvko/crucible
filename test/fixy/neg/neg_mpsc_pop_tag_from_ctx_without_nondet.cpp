// The bare tag of Nondet comes only from a context whose row holds Nondet.
// A function under the drain context, which holds Bg and Alloc, cannot
// mint the tag, so it cannot pop an MPSC channel.  The failure is the
// constraint of mint_from_ctx, at the call.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <foundation/effects/Capability.h>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel = fixy::concurrent::PermissionedMpscChannel<int, 8, UserTag, UserBrand>;

[[maybe_unused]] void attempt(fixy::BgDrainCtx const& drain, Channel::ConsumerHandle& consumer) {
    [[maybe_unused]] auto item = consumer.try_pop(foundation::effects::extract_bare(
        foundation::effects::mint_from_ctx<foundation::effects::Effect::Nondet>(drain)));
}

}  // namespace

int main() { return 0; }
