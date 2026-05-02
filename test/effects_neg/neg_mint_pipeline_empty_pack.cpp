// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 2 audit round 2 (CLAUDE.md §XXI HS14): pipeline_chain<>
// requires sizeof...(Stages) >= 1.  An empty pipeline (zero stages)
// is semantically meaningless — there's nothing to spawn or join,
// no payload type to validate, no useful work performed.
//
// Violation: calls mint_pipeline with NO stages — only the ctx.  The
// CtxFitsPipeline gate's pipeline_chain conjunct rejects sizeof...(Stages)
// == 0.  Distinct from non_stage / chain_mismatch / non_exec_ctx
// failure axes — this is the cardinality gate.
//
// Note: a pipeline of N=1 (single stage) IS admitted (vacuously
// chained — no adjacent pairs to check).  Only N=0 is rejected.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipeline / pipeline_chain.

#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/ExecCtx.h>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;

int main() {
    eff::HotFgCtx ctx;

    auto bad = conc::mint_pipeline(ctx);  // <-- empty stages pack
    (void)bad;
    return 0;
}
