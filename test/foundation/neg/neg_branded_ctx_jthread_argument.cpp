// A branded foreground context is not an argument of a new thread.
//
// VIOLATION: the context goes to std::jthread by value.  The thread
// constructor makes a copy, and the new thread holds no claim of the
// brand.
//
// Expected diagnostic: std::jthread refuses the argument, because the
// context has no copy.

#include <foundation/effects/Ctx.h>

#include <thread>

namespace {
struct Brand {};

using BrandedCtx =
    ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::BrandedFg<Brand>, ::foundation::effects::Row<>>;

void use_context(BrandedCtx const& fg) noexcept { static_cast<void>(fg); }
}  // namespace

int main() {
    const auto fg = ::foundation::effects::testing::foreground<Brand>();
    std::jthread worker(use_context, fg);
    return 0;
}
