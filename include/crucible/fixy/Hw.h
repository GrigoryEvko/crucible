#pragma once

#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/grant/_Ctrl.h>

#include <crucible/algebra/lattices/_BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/_MemoryScopeLattice.h>
#include <crucible/safety/Vendor.h>
#include <crucible/permissions/_Permission.h>

#include <crucible/effects/_ExecCtx.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::hw {

// Locality is the temporal hint for Prefetch.  0 is a streaming access with
// no reuse, 3 is high reuse.
enum class CacheOp : std::uint8_t {
    Flush,  // clflush — flush and invalidate the line
    FlushOpt,  // clflushopt — weakly ordered flush
    Writeback,  // clwb — write back, the line stays valid
    Invalidate,  // clinvalidate class
    Prefetch,  // prefetcht0 through prefetchnta
};

enum class BarrierArch : std::uint8_t {
    X86,  // lfence / sfence / mfence
    Arm,  // dmb ish / dmb osh / dmb sy / dmb ld / dmb st
    Compiler,  // asm volatile("":::"memory") + std::atomic ordering
    Gpu,  // PTX fence.{cta,cluster,gpu,sys} / membar
};

enum class TscMode : std::uint8_t {
    NotAllowed,
    SerializedPinned,  // rdtscp + lfence
    Raw,  // rdtsc, not serialized
    SteadyClockFallback,  // chrono::steady_clock — wall-clock nanoseconds, not cycles
};

enum class RngSource : std::uint8_t {
    NotAllowed,
    PhiloxCounter,  // Philox4x32 counter-based, deterministic
    OsGetrandom,
    RdRand,  // rdrand — on-die DRBG
    RdSeed,  // rdseed — on-die entropy source
};

using BarrierStrength = ::crucible::algebra::lattices::BarrierStrength;

using MemoryScope = ::crucible::algebra::lattices::MemoryScope;

using VendorBackend = ::crucible::safety::VendorBackend_v;

// A width of 0 denotes the scalar class.
template <std::uint16_t WidthBits>
inline constexpr bool valid_simd_width_v = (WidthBits == 0 || WidthBits == 128 || WidthBits == 256 || WidthBits == 512);

// The cpuid leaves sanctioned for capability detection, all side-effect free.
template <std::uint32_t Leaf>
inline constexpr bool is_sanctioned_cpuid_leaf_v = Leaf == 0x00000000u  // max basic leaf + vendor string
                                                || Leaf == 0x00000001u  // feature flags (SSE/AVX/...)
                                                || Leaf == 0x00000007u  // extended features (AVX2/AVX512/...)
                                                || Leaf == 0x0000000Bu  // x2APIC topology
                                                || Leaf == 0x0000000Du  // XSAVE / extended state
                                                || Leaf == 0x00000016u  // CPU frequency
                                                || Leaf == 0x80000000u  // max extended leaf
                                                || Leaf == 0x80000001u  // extended feature flags
                                                || Leaf == 0x80000002u  // brand string part 1
                                                || Leaf == 0x80000003u  // brand string part 2
                                                || Leaf == 0x80000004u  // brand string part 3
                                                || Leaf == 0x80000008u;  // address sizes

// The size of a fixed-string NTTP counts the trailing NUL, so an empty
// rationale has size 1.
template <::crucible::fixy::grant::ctrl::rationale Reason>
inline constexpr bool rationale_nonempty_v = (Reason.size() > 1);

[[nodiscard]] constexpr bool scope_arch_trunk_consistent(MemoryScope scope, BarrierArch arch) noexcept {
    namespace ml = ::crucible::algebra::lattices;
    if (scope == MemoryScope::Thread || scope == MemoryScope::System) {
        return true;  // realizable on any fence dialect
    }
    if (ml::mem_scope_is_accel(scope)) {
        return arch == BarrierArch::Gpu;  // PTX `.cta`/`.cluster`/`.gpu`
    }
    if (ml::mem_scope_is_arm(scope)) {
        return arch == BarrierArch::Arm;  // DMB ISH / OSH
    }
    return false;  // unreachable: every scope is a sentinel, accel or ARM
}

// A TSC read on an unpinned thread can migrate cores and land on a different
// counter.  This token is the caller's witness that the thread is pinned.
struct CpuPinProof final {
    constexpr CpuPinProof() noexcept = default;
};

struct root {};

}  // namespace crucible::fixy::hw

namespace crucible::fixy::grant::hw {

namespace fh = ::crucible::fixy::hw;

template <fh::CacheOp Op, int Locality = 0>
    requires(Locality >= 0 && Locality <= 3)
struct cache final : grant_base {};

template <fh::BarrierArch Arch, fh::BarrierStrength Kind>
struct barrier final : grant_base {};

template <fh::TscMode Mode>
struct tsc final : grant_base {};

template <fh::RngSource Source>
struct rng final : grant_base {};

template <std::uint32_t Leaf>
    requires fh::is_sanctioned_cpuid_leaf_v<Leaf>
struct cpuid final : grant_base {};

template <std::uint32_t MsrId>
struct msr final : grant_base {};

template <std::uint16_t Port>
struct port_io final : grant_base {};

template <ctrl::rationale Reason>
struct asm_ final : grant_base {};

template <std::uint16_t WidthBits>
    requires fh::valid_simd_width_v<WidthBits>
struct simd_width final : grant_base {};

template <fh::VendorBackend Backend, ctrl::rationale Id>
struct vendor_intrinsic final : grant_base {};

// Every (Scope, Arch) pairing is instantiable as a tag so that a binding can
// declare an inconsistent one for diagnosis.  The trunk-consistency gate sits
// in the mint instead, so a synthesized grant is always coherent.
template <fh::MemoryScope Scope, fh::BarrierArch Arch>
struct scope final : grant_base {};

}  // namespace crucible::fixy::grant::hw

// A which_dim specialization must appear syntactically inside namespace
// crucible::fixy::grant.  A nested namespace does not satisfy that rule.
namespace crucible::fixy::grant {

namespace fh = ::crucible::fixy::hw;

template <fh::CacheOp Op, int Locality>
struct which_dim<hw::cache<Op, Locality>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <fh::BarrierArch Arch, fh::BarrierStrength Kind>
struct which_dim<hw::barrier<Arch, Kind>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::BarrierStrength> {};

template <fh::TscMode Mode>
struct which_dim<hw::tsc<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <fh::RngSource Source>
struct which_dim<hw::rng<Source>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint32_t Leaf>
    requires fh::is_sanctioned_cpuid_leaf_v<Leaf>
struct which_dim<hw::cpuid<Leaf>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint32_t MsrId>
struct which_dim<hw::msr<MsrId>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint16_t Port>
struct which_dim<hw::port_io<Port>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <ctrl::rationale Reason>
struct which_dim<hw::asm_<Reason>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

template <std::uint16_t WidthBits>
    requires fh::valid_simd_width_v<WidthBits>
struct which_dim<hw::simd_width<WidthBits>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SimdIsa> {
};

template <fh::VendorBackend Backend, ctrl::rationale Id>
struct which_dim<hw::vendor_intrinsic<Backend, Id>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Representation> {};

template <fh::MemoryScope Scope, fh::BarrierArch Arch>
struct which_dim<hw::scope<Scope, Arch>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::MemoryScope> {
};

using accept_default_strict_for_HwInstruction = accept_default_strict_for<dim::DimensionAxis::HwInstruction>;
using accept_default_strict_for_BarrierStrength = accept_default_strict_for<dim::DimensionAxis::BarrierStrength>;
using accept_default_strict_for_SimdIsa = accept_default_strict_for<dim::DimensionAxis::SimdIsa>;
using accept_default_strict_for_MemoryScope = accept_default_strict_for<dim::DimensionAxis::MemoryScope>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::hw {

namespace ghw = ::crucible::fixy::grant::hw;

using cache_prefetch_rw_t0 = ghw::cache<CacheOp::Prefetch, 0>;
using cache_clflushopt = ghw::cache<CacheOp::FlushOpt, 0>;
using cache_clwb = ghw::cache<CacheOp::Writeback, 0>;

using barrier_x86_lfence = ghw::barrier<BarrierArch::X86, BarrierStrength::AcquireLoad>;
using barrier_x86_sfence = ghw::barrier<BarrierArch::X86, BarrierStrength::ReleaseStore>;
using barrier_x86_mfence = ghw::barrier<BarrierArch::X86, BarrierStrength::FullFence>;
using barrier_arm_dmb_ish = ghw::barrier<BarrierArch::Arm, BarrierStrength::FullFence>;
using barrier_arm_dmb_ld = ghw::barrier<BarrierArch::Arm, BarrierStrength::AcquireLoad>;
using barrier_arm_dmb_st = ghw::barrier<BarrierArch::Arm, BarrierStrength::ReleaseStore>;
using barrier_compiler_portable = ghw::barrier<BarrierArch::Compiler, BarrierStrength::CompilerBarrier>;
using barrier_compiler_acquire = ghw::barrier<BarrierArch::Compiler, BarrierStrength::AcquireLoad>;
using barrier_compiler_release = ghw::barrier<BarrierArch::Compiler, BarrierStrength::ReleaseStore>;
using barrier_compiler_seqcst = ghw::barrier<BarrierArch::Compiler, BarrierStrength::SeqCst>;

using scope_arm_ish = ghw::scope<MemoryScope::Inner, BarrierArch::Arm>;
using scope_arm_osh = ghw::scope<MemoryScope::Outer, BarrierArch::Arm>;
using scope_arm_sy = ghw::scope<MemoryScope::System, BarrierArch::Arm>;
using scope_gpu_cta = ghw::scope<MemoryScope::Cta, BarrierArch::Gpu>;
using scope_gpu_cluster = ghw::scope<MemoryScope::Cluster, BarrierArch::Gpu>;
using scope_gpu_device = ghw::scope<MemoryScope::Gpu, BarrierArch::Gpu>;
using scope_system = ghw::scope<MemoryScope::System, BarrierArch::Compiler>;

template <typename Ctx>
concept CtxFitsHwGrant = ::crucible::effects::IsExecCtx<Ctx>;

template <typename Ctx, ::crucible::fixy::grant::ctrl::rationale Reason>
concept CtxFitsAsmMint = CtxFitsHwGrant<Ctx> && rationale_nonempty_v<Reason>;

template <typename Ctx, std::uint16_t WidthBits>
concept CtxFitsSimdWidthMint = CtxFitsHwGrant<Ctx> && valid_simd_width_v<WidthBits>;

template <typename Ctx, ::crucible::fixy::grant::ctrl::rationale Id>
concept CtxFitsVendorIntrinsicMint = CtxFitsHwGrant<Ctx> && rationale_nonempty_v<Id>;

template <typename Ctx, TscMode Mode>
concept CtxFitsTscMint = CtxFitsHwGrant<Ctx> && (Mode != TscMode::NotAllowed);

// The authority gate is the Permission<root> the mint consumes, not this
// concept.
template <typename Ctx>
concept CtxFitsMsrMint = CtxFitsHwGrant<Ctx>;

// Thread is the no-cross-thread-visibility sentinel, so a scoped fence is
// never minted for it.
template <typename Ctx, MemoryScope Scope, BarrierArch Arch>
concept CtxFitsScopedFenceMint =
    CtxFitsHwGrant<Ctx> && (Scope != MemoryScope::Thread) && scope_arch_trunk_consistent(Scope, Arch);

template <::crucible::fixy::grant::ctrl::rationale Reason, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsmMint<Ctx, Reason>
[[nodiscard]] constexpr ghw::asm_<Reason> mint_asm_grant(Ctx const&) noexcept {
    return {};
}

template <std::uint16_t WidthBits, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSimdWidthMint<Ctx, WidthBits>
[[nodiscard]] constexpr ghw::simd_width<WidthBits> mint_simd_width(Ctx const&) noexcept {
    return {};
}

// The mint takes the intrinsic id first and the tag stores the backend first.
// The backend is what fixes the tag's dimension axis.
template <::crucible::fixy::grant::ctrl::rationale Id, VendorBackend Backend, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsVendorIntrinsicMint<Ctx, Id>
[[nodiscard]] constexpr ghw::vendor_intrinsic<Backend, Id> mint_vendor_intrinsic(Ctx const&) noexcept {
    return {};
}

template <TscMode Mode, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsTscMint<Ctx, Mode>
[[nodiscard]] constexpr ghw::tsc<Mode> mint_tsc_grant(Ctx const&, CpuPinProof) noexcept {
    return {};
}

template <std::uint32_t MsrId, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMsrMint<Ctx>
[[nodiscard]] constexpr ghw::msr<MsrId> mint_msr_grant(Ctx const&, ::crucible::safety::Permission<root>&&) noexcept {
    return {};
}

template <MemoryScope Scope, BarrierArch Arch, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsScopedFenceMint<Ctx, Scope, Arch>
[[nodiscard]] constexpr ghw::scope<Scope, Arch> mint_scoped_fence(Ctx const&) noexcept {
    return {};
}

}  // namespace crucible::fixy::hw

namespace crucible::fixy::hw::detail::v257_self_test {

namespace ghw = ::crucible::fixy::grant::hw;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;
namespace eff = ::crucible::effects;

static_assert(IsGrantTag<ghw::cache<CacheOp::Flush>>);
static_assert(IsGrantTag<ghw::barrier<BarrierArch::X86, BarrierStrength::FullFence>>);
static_assert(IsGrantTag<ghw::tsc<TscMode::SerializedPinned>>);
static_assert(IsGrantTag<ghw::rng<RngSource::PhiloxCounter>>);
static_assert(IsGrantTag<ghw::cpuid<0x00000007u>>);
static_assert(IsGrantTag<ghw::msr<0x10u>>);
static_assert(IsGrantTag<ghw::port_io<0x80u>>);
static_assert(IsGrantTag<ghw::asm_<"vpternlogd fast-path">>);
static_assert(IsGrantTag<ghw::simd_width<256>>);
static_assert(IsGrantTag<ghw::vendor_intrinsic<VendorBackend::NV, "wgmma">>);
static_assert(IsGrantTag<ghw::scope<MemoryScope::Gpu, BarrierArch::Gpu>>);

static_assert(sizeof(ghw::cache<CacheOp::Prefetch, 3>) == 1);
static_assert(sizeof(ghw::barrier<BarrierArch::Arm, BarrierStrength::AcqRel>) == 1);
static_assert(sizeof(ghw::tsc<TscMode::Raw>) == 1);
static_assert(sizeof(ghw::rng<RngSource::RdRand>) == 1);
static_assert(sizeof(ghw::cpuid<0x1u>) == 1);
static_assert(sizeof(ghw::msr<0xC0000080u>) == 1);
static_assert(sizeof(ghw::port_io<0xCF8u>) == 1);
static_assert(sizeof(ghw::asm_<"x">) == 1);
static_assert(sizeof(ghw::simd_width<512>) == 1);
static_assert(sizeof(ghw::vendor_intrinsic<VendorBackend::AMD, "v_mfma">) == 1);
static_assert(sizeof(ghw::scope<MemoryScope::Inner, BarrierArch::Arm>) == 1);

static_assert(which_dim_v<ghw::cache<CacheOp::Flush>> == D::HwInstruction);
static_assert(which_dim_v<ghw::barrier<BarrierArch::X86, BarrierStrength::FullFence>> == D::BarrierStrength);
static_assert(which_dim_v<ghw::tsc<TscMode::SerializedPinned>> == D::HwInstruction);
static_assert(which_dim_v<ghw::rng<RngSource::PhiloxCounter>> == D::HwInstruction);
static_assert(which_dim_v<ghw::cpuid<0x00000007u>> == D::HwInstruction);
static_assert(which_dim_v<ghw::msr<0x10u>> == D::HwInstruction);
static_assert(which_dim_v<ghw::port_io<0x80u>> == D::HwInstruction);
static_assert(which_dim_v<ghw::asm_<"reason">> == D::HwInstruction);
static_assert(which_dim_v<ghw::simd_width<256>> == D::SimdIsa);
static_assert(which_dim_v<ghw::vendor_intrinsic<VendorBackend::NV, "wgmma">> == D::Representation);
static_assert(which_dim_v<ghw::scope<MemoryScope::Cta, BarrierArch::Gpu>> == D::MemoryScope);
static_assert(which_dim_v<scope_arm_osh> == D::MemoryScope);

// fix-36: the four hardware axes served by this header all ship grant tags.
// Fn.h includes Hw.h and exercises `grant::hw::msr` further down, yet the
// audit table Fn.h owned called HwInstruction, BarrierStrength, SimdIsa and
// MemoryScope grantless — a contradiction inside one translation unit that
// survived because the table's only guards compared a literal to itself.
static_assert(::crucible::fixy::grant::audit::grant_families_witnessed_v<ghw::msr<0x10u>,  // HwInstruction
                                                                         barrier_x86_mfence,  // BarrierStrength
                                                                         ghw::simd_width<256>,  // SimdIsa
                                                                         scope_arm_osh>,  // MemoryScope
              "Hw.h ships grant families for HwInstruction, BarrierStrength, SimdIsa "
              "and MemoryScope, so none of them may appear in "
              "grant::kAxesWithoutNonDefaultGrants.");

static_assert(!std::is_same_v<ghw::cache<CacheOp::Flush>, ghw::cache<CacheOp::Writeback>>);
static_assert(!std::is_same_v<ghw::cache<CacheOp::Prefetch, 0>, ghw::cache<CacheOp::Prefetch, 3>>);
static_assert(!std::is_same_v<ghw::tsc<TscMode::Raw>, ghw::tsc<TscMode::SerializedPinned>>);
static_assert(!std::is_same_v<ghw::rng<RngSource::RdRand>, ghw::rng<RngSource::RdSeed>>);
static_assert(!std::is_same_v<ghw::msr<0x10u>, ghw::msr<0x11u>>);
static_assert(!std::is_same_v<ghw::asm_<"a">, ghw::asm_<"b">>);
static_assert(std::is_same_v<ghw::asm_<"same">, ghw::asm_<"same">>);
static_assert(!std::is_same_v<ghw::simd_width<256>, ghw::simd_width<512>>);
static_assert(
    !std::is_same_v<ghw::vendor_intrinsic<VendorBackend::NV, "i">, ghw::vendor_intrinsic<VendorBackend::AMD, "i">>);
static_assert(!std::is_same_v<barrier_x86_mfence, barrier_arm_dmb_ish>);
static_assert(!std::is_same_v<barrier_compiler_acquire, barrier_compiler_release>);
static_assert(!std::is_same_v<ghw::scope<MemoryScope::Inner, BarrierArch::Arm>,
                              ghw::scope<MemoryScope::Outer, BarrierArch::Arm>>);
static_assert(
    !std::is_same_v<ghw::scope<MemoryScope::Gpu, BarrierArch::Gpu>, ghw::scope<MemoryScope::Gpu, BarrierArch::Arm>>);
static_assert(!std::is_same_v<scope_arm_ish, scope_arm_osh>);
static_assert(!std::is_same_v<scope_arm_osh, scope_arm_sy>);
static_assert(!std::is_same_v<scope_gpu_cta, scope_gpu_cluster>);
static_assert(!std::is_same_v<scope_gpu_cluster, scope_gpu_device>);
static_assert(!std::is_same_v<scope_arm_sy, scope_system>);

static_assert(valid_simd_width_v<0> && valid_simd_width_v<512>);
static_assert(!valid_simd_width_v<100> && !valid_simd_width_v<64>);
static_assert(is_sanctioned_cpuid_leaf_v<0x00000007u>);
static_assert(!is_sanctioned_cpuid_leaf_v<0xDEADBEEFu>);
static_assert(rationale_nonempty_v<"x">);
static_assert(!rationale_nonempty_v<"">);
static_assert(scope_arch_trunk_consistent(MemoryScope::System, BarrierArch::Arm));
static_assert(scope_arch_trunk_consistent(MemoryScope::System, BarrierArch::Compiler));
static_assert(scope_arch_trunk_consistent(MemoryScope::Thread, BarrierArch::X86));
static_assert(scope_arch_trunk_consistent(MemoryScope::Gpu, BarrierArch::Gpu));
static_assert(scope_arch_trunk_consistent(MemoryScope::Cta, BarrierArch::Gpu));
static_assert(!scope_arch_trunk_consistent(MemoryScope::Gpu, BarrierArch::Arm));
static_assert(!scope_arch_trunk_consistent(MemoryScope::Cta, BarrierArch::X86));
static_assert(scope_arch_trunk_consistent(MemoryScope::Inner, BarrierArch::Arm));
static_assert(scope_arch_trunk_consistent(MemoryScope::Outer, BarrierArch::Arm));
static_assert(!scope_arch_trunk_consistent(MemoryScope::Inner, BarrierArch::Gpu));
static_assert(!scope_arch_trunk_consistent(MemoryScope::Outer, BarrierArch::Compiler));

constexpr eff::TestRunnerCtx ctx{};

static_assert(std::is_same_v<decltype(mint_asm_grant<"vpshufb hot probe">(ctx)), ghw::asm_<"vpshufb hot probe">>);
static_assert(std::is_same_v<decltype(mint_simd_width<256>(ctx)), ghw::simd_width<256>>);
static_assert(std::is_same_v<decltype(mint_vendor_intrinsic<"wgmma", VendorBackend::NV>(ctx)),
                             ghw::vendor_intrinsic<VendorBackend::NV, "wgmma">>);
static_assert(std::is_same_v<decltype(mint_tsc_grant<TscMode::SerializedPinned>(ctx, CpuPinProof{})),
                             ghw::tsc<TscMode::SerializedPinned>>);
static_assert(
    std::is_same_v<decltype(mint_msr_grant<0xC0000080u>(ctx, ::crucible::safety::mint_permission_root<root>())),
                   ghw::msr<0xC0000080u>>);
static_assert(std::is_same_v<decltype(mint_scoped_fence<MemoryScope::Gpu, BarrierArch::Gpu>(ctx)),
                             ghw::scope<MemoryScope::Gpu, BarrierArch::Gpu>>);
static_assert(std::is_same_v<decltype(mint_scoped_fence<MemoryScope::Outer, BarrierArch::Arm>(ctx)),
                             ghw::scope<MemoryScope::Outer, BarrierArch::Arm>>);

static_assert(CtxFitsAsmMint<eff::TestRunnerCtx, "x">);
static_assert(!CtxFitsAsmMint<eff::TestRunnerCtx, "">);
static_assert(!CtxFitsAsmMint<int, "x">);
static_assert(CtxFitsSimdWidthMint<eff::TestRunnerCtx, 256>);
static_assert(!CtxFitsSimdWidthMint<eff::TestRunnerCtx, 100>);
static_assert(CtxFitsTscMint<eff::TestRunnerCtx, TscMode::SerializedPinned>);
static_assert(!CtxFitsTscMint<eff::TestRunnerCtx, TscMode::NotAllowed>);
static_assert(CtxFitsVendorIntrinsicMint<eff::TestRunnerCtx, "wgmma">);
static_assert(!CtxFitsVendorIntrinsicMint<eff::TestRunnerCtx, "">);
static_assert(CtxFitsScopedFenceMint<eff::TestRunnerCtx, MemoryScope::Gpu, BarrierArch::Gpu>);
static_assert(CtxFitsScopedFenceMint<eff::TestRunnerCtx, MemoryScope::Inner, BarrierArch::Arm>);
static_assert(CtxFitsScopedFenceMint<eff::TestRunnerCtx, MemoryScope::System, BarrierArch::Compiler>);
static_assert(!CtxFitsScopedFenceMint<eff::TestRunnerCtx, MemoryScope::Thread, BarrierArch::Arm>);
static_assert(!CtxFitsScopedFenceMint<eff::TestRunnerCtx, MemoryScope::Gpu, BarrierArch::Arm>);
static_assert(!CtxFitsScopedFenceMint<int, MemoryScope::Gpu, BarrierArch::Gpu>);

static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_HwInstruction> == D::HwInstruction);
static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_BarrierStrength> == D::BarrierStrength);
static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_SimdIsa> == D::SimdIsa);
static_assert(which_dim_v<::crucible::fixy::grant::accept_default_strict_for_MemoryScope> == D::MemoryScope);

// Non-constant arguments defeat consteval folding.  This catches SFINAE and
// inline-body faults that the static asserts above can mask.
inline void runtime_smoke_test() {
    eff::TestRunnerCtx live_ctx{};

    [[maybe_unused]] auto asm_grant = mint_asm_grant<"runtime smoke asm">(live_ctx);
    [[maybe_unused]] auto width_grant = mint_simd_width<512>(live_ctx);
    [[maybe_unused]] auto vend_grant = mint_vendor_intrinsic<"runtime smoke intrinsic", VendorBackend::AMD>(live_ctx);
    [[maybe_unused]] auto tsc_grant = mint_tsc_grant<TscMode::SerializedPinned>(live_ctx, CpuPinProof{});
    [[maybe_unused]] auto msr_grant = mint_msr_grant<0x10u>(live_ctx, ::crucible::safety::mint_permission_root<root>());
    [[maybe_unused]] auto scope_gpu = mint_scoped_fence<MemoryScope::Gpu, BarrierArch::Gpu>(live_ctx);
    [[maybe_unused]] auto scope_osh = mint_scoped_fence<MemoryScope::Outer, BarrierArch::Arm>(live_ctx);

    [[maybe_unused]] ghw::cache<CacheOp::Prefetch, 2> prefetch{};
    [[maybe_unused]] barrier_x86_mfence fence{};
    [[maybe_unused]] ghw::rng<RngSource::PhiloxCounter> rng_tag{};
    [[maybe_unused]] ghw::cpuid<0x00000001u> cpuid_tag{};
    [[maybe_unused]] ghw::port_io<0xCF8u> port_tag{};
    [[maybe_unused]] scope_arm_sy sy_tag{};
    [[maybe_unused]] scope_gpu_cta cta_tag{};
}

}  // namespace crucible::fixy::hw::detail::v257_self_test
