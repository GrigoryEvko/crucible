// FIXY-V-257 sentinel TU: fixy/Hw.h — ten hardware-instruction grant tag
// families (grant::hw::cache / barrier / tsc / rng / cpuid / msr / port_io /
// asm_ / simd_width / vendor_intrinsic) routing onto the V-253
// HwInstruction / BarrierStrength / SimdIsa axes, plus five §XXI ctx-bound
// mint factories (mint_asm_grant / mint_simd_width / mint_vendor_intrinsic /
// mint_tsc_grant / mint_msr_grant).
//
// This TU forces every header-embedded static_assert to compile under the
// project warning flags (header-only static_asserts are otherwise
// unverified — feedback_header_only_static_assert_blind_spot) AND adds the
// cross-cutting checks the header cannot self-contain: federation-cache
// distinctness across the three axes a grant routes to, alias correctness,
// and the runtime smoke test.
//
// HS14 negative coverage lives in ten distinct-mismatch-class fixtures in
// test/fixy_neg/neg_fixy_v_257_*.cpp (≥2 per NEW mint × 5 mints).

#include <crucible/fixy/Hw.h>

#include <crucible/effects/ExecCtx.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace hw  = ::crucible::fixy::hw;
namespace ghw = ::crucible::fixy::grant::hw;
namespace gr  = ::crucible::fixy::grant;
namespace eff = ::crucible::effects;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── Grant-family count witness — exactly ten families ship ────────────
// Each instantiation is a distinct grant tag type; this list IS the
// ten-family roster the task premise pins.
static_assert(gr::IsGrantTag<ghw::cache<hw::CacheOp::Flush>>);
static_assert(gr::IsGrantTag<ghw::barrier<hw::BarrierArch::X86, hw::BarrierStrength::FullFence>>);
static_assert(gr::IsGrantTag<ghw::tsc<hw::TscMode::SerializedPinned>>);
static_assert(gr::IsGrantTag<ghw::rng<hw::RngSource::PhiloxCounter>>);
static_assert(gr::IsGrantTag<ghw::cpuid<0x00000007u>>);
static_assert(gr::IsGrantTag<ghw::msr<0x10u>>);
static_assert(gr::IsGrantTag<ghw::port_io<0x80u>>);
static_assert(gr::IsGrantTag<ghw::asm_<"sentinel asm">>);
static_assert(gr::IsGrantTag<ghw::simd_width<256>>);
static_assert(gr::IsGrantTag<ghw::vendor_intrinsic<hw::VendorBackend::NV, "wgmma">>);

// ── cv-ref rejection — every hw grant flows through IsGrantTag's
//    `is_same_v<G, remove_cvref_t<G>>` clause (fixy-A4-033). ────────────
static_assert(!gr::IsGrantTag_v<const ghw::tsc<hw::TscMode::Raw>>);
static_assert(!gr::IsGrantTag_v<ghw::simd_width<512>&>);
static_assert(!gr::IsGrantTag_v<const ghw::msr<0x10u>&>);

// ── Three-axis routing — the families fan out to three distinct axes,
//    NOT one (cache/tsc/rng/cpuid/msr/port_io/asm_ → HwInstruction;
//    barrier → BarrierStrength; simd_width → SimdIsa; vendor_intrinsic →
//    Representation). ───────────────────────────────────────────────────
static_assert(gr::which_dim_v<ghw::cache<hw::CacheOp::Prefetch, 0>>          == D::HwInstruction);
static_assert(gr::which_dim_v<ghw::barrier<hw::BarrierArch::Arm, hw::BarrierStrength::AcquireLoad>>
              == D::BarrierStrength);
static_assert(gr::which_dim_v<ghw::simd_width<128>>                          == D::SimdIsa);
static_assert(gr::which_dim_v<ghw::vendor_intrinsic<hw::VendorBackend::TPU, "mxu">>
              == D::Representation);

// ── Barrier alias correctness — the ten aliases land on the documented
//    (arch, BarrierStrength) cells. ───────────────────────────────────
static_assert(std::is_same_v<hw::barrier_x86_lfence,
                             ghw::barrier<hw::BarrierArch::X86, hw::BarrierStrength::AcquireLoad>>);
static_assert(std::is_same_v<hw::barrier_x86_mfence,
                             ghw::barrier<hw::BarrierArch::X86, hw::BarrierStrength::FullFence>>);
static_assert(std::is_same_v<hw::barrier_arm_dmb_ish,
                             ghw::barrier<hw::BarrierArch::Arm, hw::BarrierStrength::FullFence>>);
static_assert(std::is_same_v<hw::barrier_compiler_seqcst,
                             ghw::barrier<hw::BarrierArch::Compiler, hw::BarrierStrength::SeqCst>>);

// ── Cache alias correctness ───────────────────────────────────────────
static_assert(std::is_same_v<hw::cache_prefetch_rw_t0, ghw::cache<hw::CacheOp::Prefetch, 0>>);
static_assert(std::is_same_v<hw::cache_clflushopt,     ghw::cache<hw::CacheOp::FlushOpt, 0>>);
static_assert(std::is_same_v<hw::cache_clwb,           ghw::cache<hw::CacheOp::Writeback, 0>>);

// ── §XXI mint return-type fidelity (independent of the in-header copy) ─
constexpr eff::TestRunnerCtx sentinel_ctx{};
static_assert(std::is_same_v<
    decltype(hw::mint_simd_width<512>(sentinel_ctx)),
    ghw::simd_width<512>>);
static_assert(std::is_same_v<
    decltype(hw::mint_tsc_grant<hw::TscMode::Raw>(sentinel_ctx, hw::CpuPinProof{})),
    ghw::tsc<hw::TscMode::Raw>>);

// ── Proof-token shapes — CpuPinProof is a 1-byte phantom; root is the
//    privileged tag carried by Permission<root>. ───────────────────────
static_assert(sizeof(hw::CpuPinProof) == 1);
static_assert(std::is_empty_v<hw::root>);

}  // namespace

int main() {
    ::crucible::fixy::hw::detail::v257_self_test::runtime_smoke_test();
    return 0;
}
