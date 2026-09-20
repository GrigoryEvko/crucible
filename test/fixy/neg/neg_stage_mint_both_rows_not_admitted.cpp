// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Both payloads carry an effect and the foreground context admits none.
// The gate checks both directions, so hiding an effectful input and an
// effectful output behind the empty row is refused.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/effects/Computation.h>

#include <optional>
#include <utility>

namespace {

namespace eff = foundation::effects;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using BgPayload = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

inline void stage(FakeConsumer<IoPayload>&&, FakeProducer<BgPayload>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx;
    FakeConsumer<IoPayload> in;
    FakeProducer<BgPayload> out;

    auto bad = fixy::concurrent::mint_stage<&stage>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
