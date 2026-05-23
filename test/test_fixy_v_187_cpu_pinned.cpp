// FIXY-V-187 sentinel TU: safety/CpuPinned.h — the MOVE-ONLY proof token
// composing the V-182 affinity axis (AffinityLattice, 256-bit mask) with a
// new PinningPosture axis {NotPinned, PinnedAuto, PinnedExplicit}, plus
// safety/IsCpuPinned.h (concept extractor) and the
// row_hash_contribution<CpuPinned<Mask, Posture, T>> federation-cache
// discriminator (salt 0x32 — specialized in CpuPinned.h itself because Mask
// is a CLASS NTTP; only the salt constant is centralized in RowHashFold.h).
//
// This TU forces every header-embedded static_assert to compile under the
// project warning flags AND adds the cross-cutting checks the wrapper
// header cannot self-contain: row_hash distinctness (per-mask + per-posture)
// + nesting-order sensitivity, salt-disjointness from the sister
// Synchronization-neighborhood wrappers (ClockSource 0x30 / SchedClass
// 0x31), and the runtime smoke tests.
//
// CpuPinned is a MOVE-ONLY proof (copy deleted = the Linear consume-once
// discipline) — it is NOT a Graded carrier, so it has no GradedWrapper
// surface, no DimensionAxis, no DimensionTraits quadruple.
//
// HS14 negative coverage lives in three distinct-mismatch-class fixtures:
//   - neg_cpu_pinned_rdtsc_without_proof       (proof-required: non-CpuPinned at a TSC-reader gate)
//   - neg_cpu_pinned_two_bit_mask_singleton    (singleton gate: 2-core mask rejected)
//   - neg_cpu_pinned_auto_on_hotpath           (posture gate: PinnedAuto rejected at HotPath)

#include <crucible/safety/CpuPinned.h>
#include <crucible/safety/IsCpuPinned.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/SchedClass.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <type_traits>

namespace {

namespace sf  = ::crucible::safety;
namespace ex  = ::crucible::safety::extract;
namespace dg  = ::crucible::safety::diag;
using AffinityMask = ::crucible::algebra::lattices::AffinityMask;
using Posture = sf::PinningPosture;
using Cs_t = sf::ClockSource_v;
using Sp_t = sf::SchedulerPolicy_v;

inline constexpr AffinityMask kC0  = AffinityMask::single(0);
inline constexpr AffinityMask kC5  = AffinityMask::single(5);
inline constexpr AffinityMask k2   = AffinityMask::range(0, 1);

using PinnedC0 = sf::CpuPinned<kC0, Posture::PinnedExplicit, int>;
using AutoC0   = sf::CpuPinned<kC0, Posture::PinnedAuto,     int>;
using TwoBit   = sf::CpuPinned<k2,  Posture::PinnedExplicit, int>;

// ── Regime-1 sizeof preservation (Mask/Posture are NTTPs) ───────────
static_assert(sizeof(PinnedC0) == sizeof(int));
static_assert(sizeof(sf::CpuPinned<kC5, Posture::PinnedExplicit, unsigned long long>)
              == sizeof(unsigned long long));

// ── Move-only (the Linear consume-once discipline) ──────────────────
static_assert(!std::is_copy_constructible_v<PinnedC0>);
static_assert(!std::is_copy_assignable_v<PinnedC0>);
static_assert(std::is_move_constructible_v<PinnedC0>);

// ── Singleton-pin gate + posture pin-strength ───────────────────────
static_assert( PinnedC0::is_singleton_pin);
static_assert(!TwoBit::is_singleton_pin,
    "FIXY-V-187: a 2-core mask is NOT a singleton — the TSC-reader gate rejects it.");
static_assert( PinnedC0::meets_posture<Posture::PinnedExplicit>);
static_assert(!AutoC0::meets_posture<Posture::PinnedExplicit>,
    "FIXY-V-187: PinnedAuto does not meet a PinnedExplicit floor.");

// ── IsCpuPinned concept extractor ───────────────────────────────────
static_assert(ex::IsCpuPinned<PinnedC0>);
static_assert(!ex::IsCpuPinned<int>);
static_assert(std::is_same_v<ex::cpu_pinned_value_t<PinnedC0>, int>);
static_assert(ex::cpu_pinned_posture_v<AutoC0> == Posture::PinnedAuto);

// ── row_hash distinctness — per-mask / per-posture / vs bare ────────
static_assert(dg::row_hash_contribution_v<PinnedC0>
              != dg::row_hash_contribution_v<AutoC0>,
    "different postures MUST hash to distinct federation-cache slots.");
static_assert(dg::row_hash_contribution_v<PinnedC0>
              != dg::row_hash_contribution_v<sf::CpuPinned<kC5, Posture::PinnedExplicit, int>>,
    "different pinned cores MUST hash to distinct slots.");
static_assert(dg::row_hash_contribution_v<PinnedC0>
              != dg::row_hash_contribution_v<int>,
    "a CpuPinned proof MUST hash differently from the bare wrapped value (salt 0x32).");

// ── Salt-disjointness from sister wrappers (ClockSource 0x30 / SchedClass 0x31) ─
static_assert(dg::row_hash_contribution_v<PinnedC0>
              != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, int>>);
static_assert(dg::row_hash_contribution_v<PinnedC0>
              != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>>);

// ── Nesting-order sensitivity (§XVI / GAPS-029) — type-level only ───
static_assert(
    dg::row_hash_contribution_v<sf::CpuPinned<kC0, Posture::PinnedExplicit, sf::ClockSource<Cs_t::Boot, int>>>
    != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, sf::CpuPinned<kC0, Posture::PinnedExplicit, int>>>,
    "CpuPinned<…, ClockSource<Boot,int>> and ClockSource<Boot, CpuPinned<…,int>> "
    "MUST hash differently — row_hash is order-sensitive.");

}  // namespace

int main() {
    ::crucible::safety::detail::cpu_pinned_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_cpu_pinned_smoke_test()) return 1;
    return 0;
}
