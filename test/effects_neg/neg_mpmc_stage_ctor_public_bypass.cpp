// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for FIXY-V-011 (§XXI Universal Mint Pattern compliance).
//
// Premise: MpmcStage<FnPtr, Ctx, Inputs, Outputs> exists ONLY to be
// the load-bearing product of mint_mpmc_stage_from_endpoints<FnPtr>(
// ctx, endpoints...).  The CRUCIBLE_ROW_MISMATCH_ASSERT inside that
// mint gates the variadic input + output row union against the ctx
// row BEFORE construction.  A production-side
// `MpmcStage<...>{ctx, inputs, outputs}` direct-construction would
// emit a stage whose effect row never sees `Subrow<required, ctx>`.
//
// Fix shape (shipped in FIXY-V-011): MpmcStage's previously-public
// ctor moved to `private:`, and the sole authorized caller —
// `detail::make_mpmc_stage_from_endpoint_tuple` (the helper that
// `mint_mpmc_stage_from_endpoints` forwards to) — is friended.  Any
// direct-construction call site is now ill-formed.
//
// Diagnostic regex: "is private within this context".

#include <crucible/concurrent/Stage.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>
#include <tuple>
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

// Two-input, one-output stage body — the simplest MpmcStage shape
// that exercises the variadic input / output handle packs.
inline void fan_in_two(FakeConsumer<int>&&,
                       FakeConsumer<int>&&,
                       FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;
    std::tuple<FakeConsumer<int>, FakeConsumer<int>> inputs{};
    std::tuple<FakeProducer<int>> outputs{};

    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct construction of MpmcStage<&fan_in_two, ...> bypasses
    // mint_mpmc_stage_from_endpoints and therefore bypasses the
    // variadic row admission check.  With the V-011 fix, the ctor
    // is private and this line is ill-formed.
    conc::MpmcStage<&fan_in_two,
                    eff::HotFgCtx,
                    std::tuple<FakeConsumer<int>, FakeConsumer<int>>,
                    std::tuple<FakeProducer<int>>>
        stage{ctx, std::move(inputs), std::move(outputs)};

    return 0;
}
