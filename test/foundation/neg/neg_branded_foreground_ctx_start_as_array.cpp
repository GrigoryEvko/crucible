// A branded foreground context is not started over bytes.
//
// VIOLATION: a translation unit starts the lifetime of the foreground
// context of a brand over a zeroed buffer, and skips the claim.
//
// Expected diagnostic: no matching call to start_as_array, because
// ImplicitLifetimeThroughout is not satisfied.

#include <foundation/Lifetime.h>
#include <foundation/effects/Ctx.h>

namespace {
struct Brand {};
}  // namespace

int main() {
    namespace fe = ::foundation::effects;
    alignas(8) unsigned char bytes[8]{};
    auto const forged =
        ::foundation::lifetime::start_as_array<fe::ExecCtx<fe::ctx_cap::BrandedFg<Brand>, fe::Row<>>>(bytes, 1);
    static_cast<void>(forged);
    return 0;
}
