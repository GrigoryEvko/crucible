// FIXY-V-179 sentinel TU: the 7 perf-hub mint_*_syscall_grants
// declarations — type-level audit-trail enumerating every privileged
// Linux syscall each perf hub's load() issues.  Mirrors V-180's
// warden/Hardening.h structure (single sentinel TU + N-tuple cardinality
// pins).
//
// V-179 extends V-098's SyscallId catalog (41 → 43) with two new
// enumerators (bpf=41, perf_event_open=42, both Privilege-tier) so
// that the 7 perf hubs can carry per<bpf> + per<perf_event_open> + per<mmap>
// classification.
//
// HS14 floor: 2 neg-compile fixtures at test/fixy_neg/:
//   - neg_fixy_v_179_perf_hubs_bg_drain_reject.cpp
//       (Bg-cap context, NOT Init — mismatch axis #1)
//   - neg_fixy_v_179_perf_hubs_hot_fg_reject.cpp
//       (Fg-cap context, NOT Init — mismatch axis #2)

#include <crucible/perf/SenseHub.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/LockContention.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SyscallTpBtf.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/fixy/syscall/Bridge.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace {

namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
namespace eff = ::crucible::effects;
namespace fxbr = ::crucible::fixy::syscall::bridge;

// ── SyscallId universe extension — append-only ordinals ─────────────
//
// V-179's two additions sit at ordinals 41..42, immediately after
// V-180's prctl=40.  Append-only invariant: every existing ordinal
// (0..40) keeps its value forever — federation cache keys never drift
// across V-179.
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::bpf)             == 41);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::perf_event_open) == 42);

// ── family_of classifier — every new enumerator routed correctly ────
static_assert(fsc::family_of(fsc::SyscallId::bpf)             == fll::SyscallFamily::Privilege);
static_assert(fsc::family_of(fsc::SyscallId::perf_event_open) == fll::SyscallFamily::Privilege);

// Cross-check that pre-existing mmap (V-098 ordinal 21) still classifies.
static_assert(fsc::family_of(fsc::SyscallId::mmap) == fll::SyscallFamily::MemoryMapping);

// Cross-check that V-179/V-180's Privilege-tier additions are distinct
// types — federation cache key per syscall.
static_assert(!std::is_same_v<
    fsc::per<fsc::SyscallId::bpf>,
    fsc::per<fsc::SyscallId::perf_event_open>>);
static_assert(!std::is_same_v<
    fsc::per<fsc::SyscallId::bpf>,
    fsc::per<fsc::SyscallId::prctl>>);
static_assert(!std::is_same_v<
    fsc::per<fsc::SyscallId::perf_event_open>,
    fsc::per<fsc::SyscallId::ptrace>>);

// ── Per-hub structural witnesses ────────────────────────────────────
//
// Each of the 7 perf hubs declares `mint_<hub>_syscall_grants` =
// std::tuple<per<bpf>, per<perf_event_open>, per<mmap>>.  Verify each
// hub's tuple cardinality, slot-by-slot grant types, and V-100
// Bridge.h row-lift map.

using BpfRow = ::crucible::effects::Row<
    ::crucible::effects::Effect::IO,
    ::crucible::effects::Effect::Block>;
using MmapRow = ::crucible::effects::Row<::crucible::effects::Effect::IO>;

#define FIXY_V_179_AUDIT_HUB(HubAlias)                                            \
    do {                                                                          \
        using HG = ::crucible::perf::HubAlias;                                    \
        static_assert(std::tuple_size_v<HG> == 3,                                 \
            "FIXY-V-179: " #HubAlias " must enumerate exactly 3 grants "          \
            "(bpf + perf_event_open + mmap).  Any drift between the declared "    \
            "set and the actual syscall surface is a permission/admission "       \
            "soundness regression.");                                             \
        static_assert(std::is_same_v<std::tuple_element_t<0, HG>,                 \
                      fsc::per<fsc::SyscallId::bpf>>);                            \
        static_assert(std::is_same_v<std::tuple_element_t<1, HG>,                 \
                      fsc::per<fsc::SyscallId::perf_event_open>>);                \
        static_assert(std::is_same_v<std::tuple_element_t<2, HG>,                 \
                      fsc::per<fsc::SyscallId::mmap>>);                           \
        static_assert(std::is_same_v<                                             \
            fxbr::lift_syscall_grant_row_t<                                       \
                fsc::per<fsc::SyscallId::bpf>>,                                   \
            BpfRow>);                                                             \
        static_assert(std::is_same_v<                                             \
            fxbr::lift_syscall_grant_row_t<                                       \
                fsc::per<fsc::SyscallId::perf_event_open>>,                       \
            BpfRow>);                                                             \
        static_assert(std::is_same_v<                                             \
            fxbr::lift_syscall_grant_row_t<                                       \
                fsc::per<fsc::SyscallId::mmap>>,                                  \
            MmapRow>);                                                            \
    } while (0)

[[maybe_unused]] inline void audit_all_hubs() noexcept {
    FIXY_V_179_AUDIT_HUB(mint_sense_hub_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_pmu_sample_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_lock_contention_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_sched_switch_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_sched_tp_btf_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_syscall_tp_btf_syscall_grants);
    FIXY_V_179_AUDIT_HUB(mint_syscall_latency_syscall_grants);
}

#undef FIXY_V_179_AUDIT_HUB

// ── Per-mint Ctx-fit witnesses ──────────────────────────────────────
//
// Every perf hub's CtxFits*Mint concept admits ColdInitCtx and
// rejects BgDrainCtx + HotFgCtx — the Init-row pass-through is the
// gate, hot fg and bg drain are out-of-bounds.
static_assert(::crucible::perf::CtxFitsSenseHubMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSenseHubMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSenseHubMint<eff::HotFgCtx>);

static_assert(::crucible::perf::CtxFitsPmuSampleMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsPmuSampleMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsPmuSampleMint<eff::HotFgCtx>);

static_assert(::crucible::perf::CtxFitsLockContentionMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsLockContentionMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsLockContentionMint<eff::HotFgCtx>);

static_assert(::crucible::perf::CtxFitsSchedSwitchMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSchedSwitchMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSchedSwitchMint<eff::HotFgCtx>);

static_assert(::crucible::perf::CtxFitsSchedTpBtfMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSchedTpBtfMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSchedTpBtfMint<eff::HotFgCtx>);

static_assert(::crucible::perf::CtxFitsSyscallTpBtfMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallTpBtfMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallTpBtfMint<eff::HotFgCtx>);

static_assert(::crucible::perf::CtxFitsSyscallLatencyMint<eff::ColdInitCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallLatencyMint<eff::BgDrainCtx>);
static_assert(!::crucible::perf::CtxFitsSyscallLatencyMint<eff::HotFgCtx>);

}  // namespace

int main() {
    // Compile-time-only surface — runtime invocation requires
    // CAP_BPF / CAP_PERFMON which can't be exercised in unprivileged
    // CI.  The sentinel TU forces every header-embedded static_assert
    // through project warnings-as-errors per
    // feedback_header_only_static_assert_blind_spot.
    return 0;
}
