// test_fixy_v_264_hw_grants.cpp — positive sentinel for FIXY-V-264.
//
// Two production sites declare a hardware-axis grant for the compile-time
// hardware construct they emit, on DIFFERENT axes:
//   * TraceRing::try_append's four __builtin_prefetch calls → a cache
//     instruction → grant::hw::cache<Prefetch, 3> on the HwInstruction
//     axis (tracering_hw block).
//   * ChaseLevDeque's two Lê 2013 §3.3 seq_cst fences → a memory fence →
//     barrier<Compiler, SeqCst> on the BarrierStrength axis (chaselev_hw).
// This TU pins both declaration surfaces: the active grants are well-formed,
// route to the correct (and DISTINCT) DimensionAxis, and the wired prefetch
// locality is the declared constant.  It then runs a real ChaseLevDeque
// round-trip, ODR-using the chaselev_hw-bearing deque (the V-262 header-
// only-static_assert-blind-spot lesson applied to both hot headers).

#include <crucible/TraceRing.h>
#include <crucible/concurrent/ChaseLevDeque.h>

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Hw.h>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace {

namespace fg = ::crucible::fixy::grant;
namespace th = ::crucible::tracering_hw;
namespace ch = ::crucible::concurrent::chaselev_hw;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── TraceRing prefetch grant — HwInstruction axis ──────────────────
static_assert(fg::IsGrantTag<th::ActiveCacheGrant>,
    "FIXY-V-264: the TraceRing cache grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<th::ActiveCacheGrant> == D::HwInstruction,
    "FIXY-V-264: the TraceRing cache grant must route to the HwInstruction axis.");
static_assert(th::kPrefetchLocality == 3,
    "FIXY-V-264: the wired prefetch locality must be 3 (highest reuse) — the "
    "value the four __builtin_prefetch calls share with the grant tag.");

// ── ChaseLevDeque barrier grant — BarrierStrength axis ─────────────
static_assert(fg::IsGrantTag<ch::ActiveBarrierGrant>,
    "FIXY-V-264: the ChaseLevDeque barrier grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<ch::ActiveBarrierGrant> == D::BarrierStrength,
    "FIXY-V-264: the ChaseLevDeque barrier grant must route to the BarrierStrength axis.");

// ── The two V-264 sites occupy DISTINCT hardware axes ──────────────
// (cache on HwInstruction, fence on BarrierStrength) — unlike V-262/V-263
// which both sit on SimdIsa.  A copy-paste that put the barrier on the
// cache's axis would trip this.
static_assert(fg::which_dim_v<th::ActiveCacheGrant>
                  != fg::which_dim_v<ch::ActiveBarrierGrant>,
    "FIXY-V-264: the cache and barrier grants must occupy distinct axes.");

}  // namespace

int main() {
    // Runtime smoke — a real ChaseLevDeque push/pop round-trip ODR-uses the
    // deque whose header carries the chaselev_hw barrier declaration.
    ::crucible::concurrent::ChaseLevDeque<int, 16> deque{};
    if (!deque.push_bottom(7)) return 1;
    const std::optional<int> popped = deque.pop_bottom();
    if (!popped.has_value() || *popped != 7) return 1;

    // ODR-use the TraceRing-side declared locality constant.
    return (th::kPrefetchLocality == 3) ? 0 : 1;
}
