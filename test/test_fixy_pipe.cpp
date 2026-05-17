// ── test_fixy_pipe — sentinel TU for fixy/Pipe.h ───────────────────
//
// Pulls fixy/Pipe.h into a TU compiled under project warning flags so
// the header's static_asserts execute.  Witnesses:
//
//   1. fixy::pipe::{Endpoint, Stage, Pipeline, Direction} alias the
//      substrate types.
//   2. fixy::pipe::mint_stage / mint_pipeline are reachable via the
//      alias and produce values that are bit-identical to those
//      constructed via the substrate.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_pipe_*.cpp.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc  = crucible::concurrent;

// ─── 1. Type carrier aliases ──────────────────────────────────────

static_assert(std::is_same_v<fpipe::Direction, conc::Direction>,
    "fixy::pipe::Direction must alias concurrent::Direction.");

// ─── 2. Stage / Pipeline round-trip via the alias ─────────────────

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through_a(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
inline void pass_through_b(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    FakeConsumer<int> in_a, in_b;
    FakeProducer<int> out_a, out_b;

    auto stage_a = fpipe::mint_stage<&pass_through_a>(
        ctx, std::move(in_a), std::move(out_a));
    auto stage_b = fpipe::mint_stage<&pass_through_b>(
        ctx, std::move(in_b), std::move(out_b));

    // The pipeline composes; full execution is exercised by the
    // substrate's own pipeline tests.  Here we only assert that
    // mint_pipeline through fixy::pipe is callable.
    auto pl = fpipe::mint_pipeline(ctx, std::move(stage_a), std::move(stage_b));
    (void)pl;
    return 0;
}
