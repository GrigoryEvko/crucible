// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_pipeline with a bare int where the coordinating context belongs.
// The stage was minted under a real context; only the coordinator is
// wrong.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>
#include <utility>

namespace {

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx stage_ctx;
    auto stage = fixy::concurrent::mint_stage<&pass_through>(stage_ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    int not_a_ctx = 0;

    auto bad = fixy::concurrent::mint_pipeline(not_a_ctx, std::move(stage));
    (void)bad;
    return 0;
}
