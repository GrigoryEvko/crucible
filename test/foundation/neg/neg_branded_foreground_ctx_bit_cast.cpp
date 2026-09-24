// A branded foreground context is not built from a byte.
//
// VIOLATION: a translation unit reinterprets one byte as the foreground
// context of a brand, and skips the claim of that brand.
//
// Expected diagnostic: no matching call to bit_cast, because the target
// is not trivially copyable.

#include <foundation/effects/Ctx.h>

#include <bit>

namespace {
struct Brand {};
}  // namespace

int main() {
    namespace fe = ::foundation::effects;
    auto const forged = std::bit_cast<fe::ExecCtx<fe::ctx_cap::BrandedFg<Brand>, fe::Row<>>>(char{0});
    static_cast<void>(forged);
    return 0;
}
