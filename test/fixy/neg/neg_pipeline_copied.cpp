// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A pipeline holds move-only stages, so a copy would duplicate every
// linear token they hold.  The copy constructor is deleted with that
// reason, and the compiler repeats it.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>
#include <utility>

namespace {

// The two stages chain, so their handles name one channel.
template <typename T>
struct FakeChannel {};

template <typename T>
struct FakeConsumer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    auto first = fixy::concurrent::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto second = fixy::concurrent::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    const fixy::BgDrainCtx coordinator{::foundation::effects::testing::bg()};
    auto pipeline = fixy::concurrent::mint_pipeline(coordinator, std::move(first), std::move(second));

    auto twin = pipeline;
    (void)twin;
    return 0;
}
