// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags.

#include <crucible/fixy/Hw.h>

#include <crucible/effects/ExecCtx.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace hw = ::crucible::fixy::hw;
namespace ghw = ::crucible::fixy::grant::hw;
namespace gr = ::crucible::fixy::grant;
namespace eff = ::crucible::effects;
using D = ::crucible::fixy::dim::DimensionAxis;

// The list is the full grant-family roster. A new family adds a line here.
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

static_assert(!gr::IsGrantTag_v<const ghw::tsc<hw::TscMode::Raw>>);
static_assert(!gr::IsGrantTag_v<ghw::simd_width<512>&>);
static_assert(!gr::IsGrantTag_v<const ghw::msr<0x10u>&>);

static_assert(gr::which_dim_v<ghw::cache<hw::CacheOp::Prefetch, 0>> == D::HwInstruction);
static_assert(gr::which_dim_v<ghw::barrier<hw::BarrierArch::Arm, hw::BarrierStrength::AcquireLoad>>
              == D::BarrierStrength);
static_assert(gr::which_dim_v<ghw::simd_width<128>> == D::SimdIsa);
static_assert(gr::which_dim_v<ghw::vendor_intrinsic<hw::VendorBackend::TPU, "mxu">> == D::Representation);

static_assert(
    std::is_same_v<hw::barrier_x86_lfence, ghw::barrier<hw::BarrierArch::X86, hw::BarrierStrength::AcquireLoad>>);
static_assert(
    std::is_same_v<hw::barrier_x86_mfence, ghw::barrier<hw::BarrierArch::X86, hw::BarrierStrength::FullFence>>);
static_assert(
    std::is_same_v<hw::barrier_arm_dmb_ish, ghw::barrier<hw::BarrierArch::Arm, hw::BarrierStrength::FullFence>>);
static_assert(
    std::is_same_v<hw::barrier_compiler_seqcst, ghw::barrier<hw::BarrierArch::Compiler, hw::BarrierStrength::SeqCst>>);

static_assert(std::is_same_v<hw::cache_prefetch_rw_t0, ghw::cache<hw::CacheOp::Prefetch, 0>>);
static_assert(std::is_same_v<hw::cache_clflushopt, ghw::cache<hw::CacheOp::FlushOpt, 0>>);
static_assert(std::is_same_v<hw::cache_clwb, ghw::cache<hw::CacheOp::Writeback, 0>>);

constexpr eff::TestRunnerCtx sentinel_ctx{};
static_assert(std::is_same_v<decltype(hw::mint_simd_width<512>(sentinel_ctx)), ghw::simd_width<512>>);
static_assert(std::is_same_v<decltype(hw::mint_tsc_grant<hw::TscMode::Raw>(sentinel_ctx, hw::CpuPinProof{})),
                             ghw::tsc<hw::TscMode::Raw>>);

static_assert(sizeof(hw::CpuPinProof) == 1);
static_assert(std::is_empty_v<hw::root>);

}  // namespace

int main() {
    ::crucible::fixy::hw::detail::v257_self_test::runtime_smoke_test();
    return 0;
}
