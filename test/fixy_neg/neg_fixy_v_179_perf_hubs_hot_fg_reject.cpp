// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-179 HS14 fixture #2 of 2 for the 7 perf-hub mints.
// CtxFitsPmuSampleMint (and its 6 siblings) reject HotFgCtx because
// the hot-foreground row carries Effect::Fg (the recording-thread
// capability — pinned, hot, L1-resident) but NOT Effect::Init.
// PmuSample::load() (and every other perf hub's load) opens per-CPU
// perf_event_open file descriptors + bpf() program load + mmaps the
// kernel ringbuf — blocking startup operations.  Engaging mint_pmu_sample
// from the hot fg thread would stall the recording loop's ~5 ns/op
// TraceRing push budget by orders of magnitude.
//
// Mismatch axis: Fg-cap context, NOT Init.
//   Distinct from fixture #1 (BgDrainCtx — Bg-cap, also NOT Init).  Both
//   are Init-rejections but exercise distinct effect-row failure paths
//   (Fg vs Bg) ⇒ HS14 floor satisfied.
//
// We exercise a DIFFERENT perf-hub mint (mint_pmu_sample) from fixture #1
// (which used mint_sense_hub) — distinct fixture surfaces, distinct
// mismatch axes.
//
// Expected diagnostic: CtxFitsPmuSampleMint / CtxOwnsCapability /
//                      Effect::Init / constraints not satisfied.

#include <crucible/perf/PmuSample.h>

namespace neg_fixy_v_179_perf_hubs_hot_fg {

namespace eff = ::crucible::effects;

// HotFgCtx admits Fg + nothing else (no Init, no Block).  Engaging
// mint_pmu_sample from the hot recording thread would stall the
// ~5 ns/op TraceRing push budget by orders of magnitude — blocking
// privileged syscalls don't belong on the hot path.
[[maybe_unused]] constexpr auto bad_dispatch =
    ::crucible::perf::mint_pmu_sample(
        eff::HotFgCtx{},
        eff::Init{});

}  // namespace neg_fixy_v_179_perf_hubs_hot_fg

int main() {
    return 0;
}
