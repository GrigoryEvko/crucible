// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Two stages that are each well formed and do not chain: the first
// produces int and the second consumes float.  Each mints on its own;
// only the adjacent pair is refused.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>
#include <utility>

namespace {

// Each handle names the channel of its payload, so the chain breaks on
// the payload and not on a handle that names no channel.
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

inline void int_pass(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
inline void float_to_double(FakeConsumer<float>&&, FakeProducer<double>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    auto first = fixy::concurrent::mint_stage<&int_pass>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto second = fixy::concurrent::mint_stage<&float_to_double>(ctx, FakeConsumer<float>{}, FakeProducer<double>{});

    const fixy::BgDrainCtx coordinator{::foundation::effects::testing::bg()};
    auto bad = fixy::concurrent::mint_pipeline(coordinator, std::move(first), std::move(second));
    (void)bad;
    return 0;
}
