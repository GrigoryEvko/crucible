// Negative compile test.  This file must fail to compile.
//
// Each door of a handle of the context form, such as a parked handle, takes
// the context of its caller.  A stage body that takes such a handle and names
// no context can call no door of it, unless it mints a context of its own,
// which its stage did not admit.  So the stage refuses the body.
//
// Expected diagnostic: the body names no context of its handles.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>

#include <cstddef>
#include <utility>

namespace neg_stage_ctx_handle_body_names_no_ctx_types {

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

inline void body(CtxConsumer&& /*in*/, Producer&& /*out*/) noexcept {}

}  // namespace neg_stage_ctx_handle_body_names_no_ctx_types

int main() {
    using namespace neg_stage_ctx_handle_body_names_no_ctx_types;
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    [[maybe_unused]] auto refused = ::fixy::concurrent::mint_stage<&body>(ctx, CtxConsumer{}, Producer{});
    return 0;
}
