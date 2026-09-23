// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The input payload is a plain struct that holds a computation engaged
// at Row<IO> in a member.  No template argument of the payload names the
// row, so only the walk over the payload's members finds it.  The
// foreground context admits no effect at all.
//
// Sibling of neg_stage_mint_input_row_not_admitted.cpp, where the
// payload is the computation itself.

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

struct IoPayload {
    int tag = 0;
    eff::Computation<eff::Row<eff::Effect::IO>, int> held;
};

inline void stage(FakeConsumer<IoPayload>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx;
    FakeConsumer<IoPayload> in;
    FakeProducer<int> out;

    auto bad = fixy::concurrent::mint_stage<&stage>(ctx, std::move(in), std::move(out));
    (void)bad;
    return 0;
}
