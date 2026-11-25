// Negative compile test.  This file must fail to compile.
//
// A stage gives its body the context that its mint got.  A body that names
// another context, here one that owns Block, would wait with an effect that
// the context of the stage does not own.  So the stage refuses the body.
//
// Expected diagnostic: the body does not take the context of the stage.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>

#include <cstddef>
#include <utility>

namespace neg_stage_body_names_another_ctx_types {

struct Wire {};

struct CtxConsumer {
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    template <class Ctx>
    [[nodiscard]] ::fixy::Option<int> try_pop(Ctx const&) noexcept {
        return {};
    }
};

struct Producer {
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

inline void body(CtxConsumer&& in, Producer&& /*out*/, ::fixy::BgLoadCtx const& ctx) noexcept {
    static_cast<void>(in.try_pop(ctx));
}

}  // namespace neg_stage_body_names_another_ctx_types

int main() {
    using namespace neg_stage_body_names_another_ctx_types;
    ::fixy::BgDrainCtx const ctx{::foundation::effects::testing::bg()};
    [[maybe_unused]] auto refused = ::fixy::concurrent::mint_stage<&body>(ctx, CtxConsumer{}, Producer{});
    return 0;
}
