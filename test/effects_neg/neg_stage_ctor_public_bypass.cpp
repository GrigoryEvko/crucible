// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for FIXY-V-010 (§XXI Universal Mint Pattern compliance).
//
// Premise: Stage<FnPtr, Ctx> exists ONLY to be the load-bearing
// product of mint_stage<FnPtr, Ctx>(ctx, in, out).  Two CRUCIBLE_
// ROW_MISMATCH_ASSERT lines inside mint_stage gate the input row
// (Subrow<input_row, ctx_row>) and the output row
// (Subrow<output_row, ctx_row>) BEFORE construction.  A production-
// side `Stage<&body, MyCtx>{ctx, in, out}` direct-construction
// would emit a Stage whose effect rows never pass through either
// check — V-010's load-bearing §XXI bypass.
//
// Fix shape (shipped in FIXY-V-010): Stage's previously-public ctor
// `Stage(Ctx const&, consumer_handle_type&&, producer_handle_type&&)`
// moved to `private:`, and `mint_stage` friended so the only
// authorized authorization point remains the mint factory.  Any
// direct construction site is now ill-formed.
//
// This fixture witnesses the gate fires: it materializes the
// handles `mint_stage` would consume and attempts the rejected
// direct-construction of Stage.  Build MUST fail; diagnostic MUST
// contain "private".

#include <crucible/concurrent/Stage.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>
#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<int> in{};
    FakeProducer<int> out{};

    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct construction of Stage<&pass_through, HotFgCtx> bypasses
    // mint_stage and therefore bypasses CtxFitsStage's two row
    // admission checks.  With the V-010 fix, the ctor is private
    // and this line is ill-formed.  Before V-010 this would have
    // compiled silently.
    conc::Stage<&pass_through, eff::HotFgCtx> stage{
        ctx, std::move(in), std::move(out)};

    return 0;
}
