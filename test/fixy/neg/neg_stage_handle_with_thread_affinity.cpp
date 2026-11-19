// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The branded foreground source belongs to the thread that holds the claim
// of its brand, so no class that refers to it moves to another thread
// (foundation/effects/Ctx.h, foundation/Transfer.h).  A stage whose consumer
// handle keeps a reference to a branded context would use the context on the
// thread of the stage.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/effects/Ctx.h>

namespace {

struct FixtureBrand {};
using BrandedCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::BrandedFg<FixtureBrand>,
                                                  ::foundation::effects::Row<>>;

template <typename T>
struct ForegroundConsumer {
    BrandedCtx const& owner;
    [[nodiscard]] ::foundation::core::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct PlainProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(ForegroundConsumer<int>&&, PlainProducer<int>&&) noexcept {}

// The mint of a stage over a handle that refers to the branded context.  The
// compiler refuses the body, although no code calls it.
[[maybe_unused]] void mint_with(BrandedCtx const& owner) {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    auto stage = fixy::concurrent::mint_stage<&pass_through>(ctx, ForegroundConsumer<int>{owner}, PlainProducer<int>{});
    (void)stage;
}

}  // namespace

int main() { return 0; }
