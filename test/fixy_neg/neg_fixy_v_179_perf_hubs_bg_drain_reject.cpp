// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-179 HS14 fixture #1 of 2 for the 7 perf-hub mints.
// CtxFitsSenseHubMint (and its 6 siblings) reject BgDrainCtx because
// the bg-drain row carries Effect::Bg + Alloc + IO + Block but NOT
// Effect::Init.  SenseHub::load() (and every other perf hub's load)
// opens per-CPU perf_event_open file descriptors + bpf() program load
// + mmaps the kernel ringbuf — startup-only operations belonging to
// the Init row.  Engaging mint_sense_hub from a bg-drain thread would
// race the warden's own startup pinning against the bg worker pool.
//
// Mismatch axis: Bg-cap context, NOT Init.
//   Distinct from fixture #2 (HotFgCtx — Fg-cap, also NOT Init).  Both
//   are Init-rejections but exercise distinct effect-row failure paths
//   (Bg vs Fg) ⇒ HS14 floor satisfied.
//
// Expected diagnostic: CtxFitsSenseHubMint / CtxOwnsCapability /
//                      Effect::Init / constraints not satisfied.

#include <crucible/perf/SenseHub.h>

namespace neg_fixy_v_179_perf_hubs_bg_drain {

namespace eff = ::crucible::effects;

// BgDrainCtx admits Bg + Alloc + IO + Block but NOT Init.  Engaging
// mint_sense_hub from a bg-drain thread is a clear category error —
// the privileged bpf()/perf_event_open/mmap startup set belongs to
// Init.
[[maybe_unused]] constexpr auto bad_dispatch =
    ::crucible::perf::mint_sense_hub(
        eff::BgDrainCtx{},
        eff::Init{});

}  // namespace neg_fixy_v_179_perf_hubs_bg_drain

int main() {
    return 0;
}
