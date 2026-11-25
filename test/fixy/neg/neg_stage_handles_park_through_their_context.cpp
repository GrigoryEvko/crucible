// Negative compile test.  This file must fail to compile.
//
// The two handles here keep a context that owns Block, and the body parks
// through it on each turn.  The context of the stage owns no Block, and the
// plain doors of the handles name no context, so no other gate reads the wait.
// The stage refuses the handles.
//
// Expected diagnostic: the handles of the body reach an effect.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/os/Futex.h>
#include <foundation/core/Ref.h>

#include <cstddef>
#include <utility>

namespace neg_stage_handles_park_through_their_context_types {

namespace cc = ::fixy::concurrent;

struct Wire {};

template <class Brand>
class WaitingConsumer {
public:
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    WaitingConsumer(::fixy::BgLoadCtx const& ctx, ::fixy::Ref<cc::EventCount, Brand> items) noexcept
        : ctx_{ctx}, items_{items} {}
    [[nodiscard]] ::fixy::Option<int> try_pop() noexcept {
        cc::wait_for<cc::SpinThenPark<0>>(ctx_, *items_, []() noexcept { return false; });
        return {};
    }

private:
    ::fixy::BgLoadCtx ctx_;
    ::fixy::Ref<cc::EventCount, Brand> items_;
};

class WaitingProducer {
public:
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    explicit WaitingProducer(::fixy::BgLoadCtx const& ctx) noexcept : ctx_{ctx} {}
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }

private:
    ::fixy::BgLoadCtx ctx_;
};

template <class Consumer>
void body(Consumer&& in, WaitingProducer&& /*out*/) noexcept {
    static_cast<void>(in.try_pop());
}

}  // namespace neg_stage_handles_park_through_their_context_types

int main() {
    using namespace neg_stage_handles_park_through_their_context_types;
    ::fixy::BgLoadCtx const wide{::foundation::effects::testing::bg()};
    ::fixy::BgDrainCtx const ctx{::foundation::effects::testing::bg()};
    cc::EventCount items{};
    auto consumer = WaitingConsumer{wide, ::fixy::to(items)};
    [[maybe_unused]] auto refused =
        cc::mint_stage<&body<decltype(consumer)>>(ctx, std::move(consumer), WaitingProducer{wide});
    return 0;
}
