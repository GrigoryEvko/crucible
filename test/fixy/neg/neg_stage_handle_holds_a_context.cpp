// Negative compile test.  This file must fail to compile.
//
// A stage runs with the effects of its context.  The consumer handle here
// keeps a context of its own, so the body could take an effect from the handle
// that the context of the stage does not own.  So the stage refuses the handle.
//
// Expected diagnostic: the handles of the body reach an effect.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>

#include <cstddef>
#include <utility>

namespace neg_stage_handle_holds_a_context_types {

struct Wire {};

class ContextConsumer {
public:
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    explicit ContextConsumer(::fixy::BgLoadCtx const& ctx) noexcept : ctx_{ctx} {}
    [[nodiscard]] ::fixy::Option<int> try_pop() noexcept { return {}; }
    [[nodiscard]] ::fixy::BgLoadCtx const& ctx() const noexcept { return ctx_; }

private:
    ::fixy::BgLoadCtx ctx_;
};

struct Producer {
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

inline void body(ContextConsumer&& /*in*/, Producer&& /*out*/) noexcept {}

}  // namespace neg_stage_handle_holds_a_context_types

int main() {
    using namespace neg_stage_handle_holds_a_context_types;
    ::fixy::BgLoadCtx const wide{::foundation::effects::testing::bg()};
    ::fixy::BgDrainCtx const ctx{::foundation::effects::testing::bg()};
    [[maybe_unused]] auto refused = ::fixy::concurrent::mint_stage<&body>(ctx, ContextConsumer{wide}, Producer{});
    return 0;
}
