// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A capability payload carries the row of the effect it grants.  An
// allocation capability sourced from the background context needs Alloc,
// and the foreground context does not have it.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/effects/Capability.h>

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

using Payload = eff::Capability<eff::Effect::Alloc, eff::Bg>;

inline void stage(FakeConsumer<Payload>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx;
    FakeConsumer<Payload> in;
    FakeProducer<int> out;

    auto bad = fixy::concurrent::mint_stage<&stage>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
