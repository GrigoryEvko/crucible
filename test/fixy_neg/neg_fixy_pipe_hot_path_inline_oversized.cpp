// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-218 HS14 fixture #1 of 2 for fixy::pipe::stance::HotPathInline:
// oversized-working-set rejection — a Pipeline whose aggregate
// per-call working set exceeds BOTH the default L1dBytes (32 KiB)
// AND L2Bytes (1 MiB) MUST be rejected by the stance.
//
// Violation: `stance::HotPathInline<P, L1d, L2>` folds the substrate
// witness `P::template will_run_inline_v<L1d, L2>()`, which checks
//
//     (aggregate_per_call_working_set <= L1dBytes)
//       || (aggregate_per_call_working_set <= L2Bytes)
//
// after the inline-safe + working-set-known early-out.  A Pipeline of
// 5 × 10 MiB-per-stage stages has aggregate 100 MiB — exceeds both
// the 32 KiB L1d default AND the 1 MiB L2 default.  The conjunct
// returns false; the concept rejects; the static_assert fires with a
// grep-discoverable diagnostic.
//
// Distinct from fixture #2 (not-inline-safe rejection):
//   * Fixture #1 — WORKING-SET-TOO-LARGE rejection.  Pipeline IS
//     inline_safe (stages opt in via stage_inline_safe specialisation)
//     and working-set IS known, but the aggregate exceeds the cache
//     budget.  Rejection rides on the (ws ≤ L1d || ws ≤ L2) conjunct.
//   * Fixture #2 — INLINE-SAFETY-MISSING rejection.  Pipeline's
//     working-set fits comfortably, but the stages DON'T opt in via
//     stage_inline_safe, so inline_safe == false and the early-out
//     fires.  Rejection rides on the !inline_safe branch.
// Two distinct rejection AXES (not just two values on the same axis)
// ⇒ HS14 floor satisfied.
//
// Without the stance, a band-3 caller declaring "this pipeline runs
// inline on the hot path" would silently fall back to thread-per-stage
// jthread fan-out at runtime — the static claim becomes a runtime lie.
// V-218 closes that hole at compile time.
//
// Expected diagnostic: static assertion failed mentioning HotPathInline
// (concept unsatisfied because will_run_inline_v<32 KiB, 1 MiB>()
// returned false for an oversized aggregate).

#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <cstddef>
#include <optional>

namespace neg_fixy_pipe_hot_path_inline_oversized {

namespace cc  = ::crucible::concurrent;
namespace eff = ::crucible::effects;

constexpr std::size_t MiB = 1024 * 1024;

// Heavy stage handle: per-call working set = 10 MiB.  Five stages
// chained together give an aggregate of 100 MiB — far above the
// default 32 KiB L1dBytes and 1 MiB L2Bytes the stance uses.

template <std::size_t Ws>
struct HeavyConsumer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct HeavyProducer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

static void heavy(HeavyConsumer<10 * MiB>&&,
                  HeavyProducer<10 * MiB>&&) noexcept {}

using HeavyStage = cc::Stage<&heavy, eff::HotFgCtx>;

}  // namespace neg_fixy_pipe_hot_path_inline_oversized

namespace crucible::concurrent {
// Opt in to inline-safety for the heavy stage — pins that the
// rejection axis is WORKING-SET-TOO-LARGE, not INLINE-SAFETY-MISSING.
// Without this specialisation the test would still red, but on the
// wrong axis (fixture #2's axis), defeating HS14's distinct-axis
// discipline.
template <>
struct stage_inline_safe<
    ::neg_fixy_pipe_hot_path_inline_oversized::HeavyStage>
    : std::true_type {};
}  // namespace crucible::concurrent

namespace neg_fixy_pipe_hot_path_inline_oversized {

using HugePipeline = cc::Pipeline<
    HeavyStage, HeavyStage, HeavyStage, HeavyStage, HeavyStage>;

// Sanity — the substrate facts the rejection depends on.
static_assert(HugePipeline::aggregate_per_call_working_set
              == 100 * MiB);
static_assert(HugePipeline::inline_safe);
static_assert(HugePipeline::aggregate_working_set_known);

}  // namespace neg_fixy_pipe_hot_path_inline_oversized

int main() {
    namespace ns = neg_fixy_pipe_hot_path_inline_oversized;

    // THE LOAD-BEARING ASSERTION: must FAIL to compile.  If the
    // substrate's will_run_inline_v ever loosened (e.g. dropped the
    // L2 cap or upper-bounded by DRAM instead), a 100 MiB pipeline
    // could falsely claim inline dispatch and a band-3 site would
    // mis-budget.  The stance's job is to catch this at compile time.
    static_assert(
        ::crucible::fixy::pipe::stance::HotPathInline<
            ns::HugePipeline>,
        "FIXY-V-218 fixture #1: 100 MiB inline-safe pipeline MUST "
        "FAIL stance::HotPathInline at the 32 KiB / 1 MiB default "
        "cache budget — aggregate working set exceeds both L1d and "
        "L2 caps in the substrate witness.");
    return 0;
}
