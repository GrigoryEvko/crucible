// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-218 HS14 fixture #2 of 2 for fixy::pipe::stance::HotPathInline:
// not-inline-safe rejection — a Pipeline whose stages have NOT been
// opted in to `stage_inline_safe` (default primary template = false)
// MUST be rejected by the stance, regardless of how small the
// per-call working set is.
//
// Violation: `Pipeline<Stages...>::will_run_inline_v<L1d, L2>()`
// short-circuits via
//
//     if constexpr (!inline_safe || !aggregate_working_set_known) {
//         return false;
//     }
//
// before the working-set cap comparisons run.  A stage that hasn't
// declared `stage_inline_safe = true` (and so inherits the primary
// template's `std::false_type`) keeps the pipeline's `inline_safe`
// fold at false; the entire WS path is skipped; the witness returns
// false; the concept rejects.
//
// This is the rejection axis for stages that MIGHT spill registers,
// take a TLS dependency, hold a mutex, do a syscall, or otherwise
// violate the no-yield assumption — declaring `stage_inline_safe`
// is an explicit promise from the stage author, and the ABSENCE of
// that promise is the load-bearing signal that the dispatcher must
// thread the stage through a jthread rather than fold it inline.
//
// Distinct from fixture #1 (working-set-too-large rejection):
//   * Fixture #1 — WORKING-SET-TOO-LARGE.  Pipeline IS inline_safe
//     and WS IS known, but aggregate exceeds the cache budget.
//     Rejects on the `(ws ≤ L1d || ws ≤ L2)` comparison branch.
//   * Fixture #2 — INLINE-SAFETY-MISSING.  Stages haven't opted in
//     via `stage_inline_safe` specialisation; the early-out fires.
//     Working set is irrelevant — even a 16-byte aggregate is
//     rejected if `inline_safe == false`.
// Two distinct rejection AXES (not just two values on the same axis)
// ⇒ HS14 floor satisfied.
//
// Expected diagnostic: static assertion failed mentioning HotPathInline
// (concept unsatisfied because will_run_inline_v<32 KiB, 1 MiB>()
// returned false via the !inline_safe early-out).

#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <cstddef>
#include <optional>

namespace neg_fixy_pipe_hot_path_inline_not_inline_safe {

namespace cc  = ::crucible::concurrent;
namespace eff = ::crucible::effects;

// Tiny stage handle — per-call working set fits in even 4 KiB of L1.
// The stage's micro-WS pins that the rejection axis is NOT
// working-set-too-large (fixture #1's axis); the only reason the
// pipeline reds is the missing `stage_inline_safe` opt-in.

template <std::size_t Ws>
struct TinyConsumer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct TinyProducer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

static void tiny(TinyConsumer<128>&&, TinyProducer<128>&&) noexcept {}

using TinyStage = cc::Stage<&tiny, eff::HotFgCtx>;

// DELIBERATELY DO NOT SPECIALISE stage_inline_safe FOR TinyStage.
// The primary template is std::false_type, so:
//   stage_inline_safe_v<TinyStage> == false
//   → Pipeline<TinyStage, ...>::inline_safe == false
//   → will_run_inline_v<...>() returns false via the !inline_safe
//     early-out
//   → stance::HotPathInline<Pipeline<TinyStage, ...>> rejects.
// This is the exact bug shape — a stage that didn't opt in
// silently dispatches via jthread fan-out at runtime, with no
// compile-time signal at the band-3 call site.  The stance closes
// that gap.

using TinyPipeline = cc::Pipeline<TinyStage, TinyStage, TinyStage>;

// Sanity — pin the substrate facts the rejection depends on.
static_assert(!TinyPipeline::inline_safe,
    "fixture precondition: TinyPipeline must NOT be inline_safe — "
    "the stage_inline_safe specialisation is deliberately absent.");

}  // namespace neg_fixy_pipe_hot_path_inline_not_inline_safe

int main() {
    namespace ns = neg_fixy_pipe_hot_path_inline_not_inline_safe;

    // THE LOAD-BEARING ASSERTION: must FAIL to compile.  If the
    // substrate's will_run_inline_v ever dropped the !inline_safe
    // early-out (or if stage_inline_safe ever silently defaulted to
    // true), a stage that needs jthread dispatch could falsely claim
    // inline fitness.  V-218's stance catches that at compile time
    // before the runtime dispatcher would have to.
    static_assert(
        ::crucible::fixy::pipe::stance::HotPathInline<
            ns::TinyPipeline>,
        "FIXY-V-218 fixture #2: pipeline of NON-inline-safe stages "
        "MUST FAIL stance::HotPathInline regardless of working-set "
        "size — !inline_safe is the early-out the substrate witness "
        "fires on.");
    return 0;
}
