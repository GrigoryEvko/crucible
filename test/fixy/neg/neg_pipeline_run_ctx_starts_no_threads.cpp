// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A background context mints the pipeline, and the foreground thread
// tries to run it.  A pipeline can move to another thread after the mint,
// so run asks the context of the caller again.  The foreground context
// owns neither Bg nor Init, so run refuses it.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>
#include <utility>

namespace {

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    const fixy::BgDrainCtx coordinator{::foundation::effects::testing::bg()};
    auto stage = fixy::concurrent::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto pipeline = fixy::concurrent::mint_pipeline(coordinator, std::move(stage));

    std::move(pipeline).run(ctx);
    return 0;
}
