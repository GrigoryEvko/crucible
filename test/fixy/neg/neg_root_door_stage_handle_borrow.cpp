// A stage names a key of a brand, and its input handle holds a View of holders
// of that brand.  The stage door would mint a key on the thread of the stage,
// and the stage could open a frame of a holder that another thread owns.  The
// entry of the root doors refuses a handle that reaches an owner of a brand of
// the keys through a borrow.
#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>
#include <cstddef>
#include <utility>
namespace {
namespace cc = ::fixy::concurrent;
struct HolderBrand final {};
struct Holder {
    ::foundation::core::FixedArray<int, 4, HolderBrand> array{};
};
struct IntChannel {};
struct Consumer {
    using channel_type = IntChannel;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 0;
    ::foundation::core::View<Holder const, 1> holders;
    [[nodiscard]] ::foundation::core::Option<int> try_pop() noexcept {
        return ::foundation::core::Option<int>::some(1);
    }
};
struct Producer {
    using channel_type = IntChannel;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 0;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};
void borrowing_stage(Consumer&&, Producer&&, ::foundation::core::LoanKey<HolderBrand>& key) noexcept {
    static_cast<void>(key);
}
}  // namespace
int main() {
    ::fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    ::fixy::BgDrainCtx const coordinator{::foundation::effects::testing::bg()};
    ::foundation::core::FixedArray<Holder, 1> const holders{};
    auto stage = cc::mint_stage<&borrowing_stage>(ctx, Consumer{holders.view()}, Producer{});
    auto pipeline = cc::mint_pipeline(coordinator, std::move(stage));
    std::move(pipeline).run(coordinator);
    return 0;
}
