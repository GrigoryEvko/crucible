// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The foreground context mints a pipeline of foreground stages.  The row
// of the stages is empty, so the row gate admits it.  The pipeline starts
// one thread per stage and joins them, and the foreground context owns
// neither Bg nor Init, so the mint refuses it.

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
    auto stage = fixy::concurrent::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    auto bad = fixy::concurrent::mint_pipeline(ctx, std::move(stage));
    (void)bad;
    return 0;
}
