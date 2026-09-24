#pragma once

#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>
#include <crucible/safety/Vendor.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::fixy::vendor {

using VendorBackend = ::crucible::safety::VendorBackend_v;

// Ordinals are append-only. A persisted hash keys on them, so append at
// the next free value inside a family and never renumber. The 100-unit
// family stride keeps isa_family_of a plain range check.
enum class IsaTag : std::uint16_t {
    SSE2 = 100,
    SSE4_1 = 101,
    AVX2 = 102,
    AVX512F = 103,
    AVX512BW = 104,
    BMI2 = 110,
    AESNI = 111,
    SHANI = 112,
    AMXTILE = 113,
    NEON = 200,
    NEON_DotProd = 201,
    NEON_FP16 = 202,
    SVE = 203,
    SVE2 = 204,
    CUDA_PTX_SM89 = 300,
    CUDA_PTX_SM90 = 301,
    CUDA_PTX_SM100 = 302,
    AMDGCN_GFX1100 = 400,
    AMDGCN_GFX942 = 401,
    TPU_TFLITE = 500,
    TRN_NEFF = 600,
};

enum class IsaFamily : std::uint8_t {
    X86,
    Arm,
    CudaPtx,
    Amdgcn,
    Tpu,
    Trn
};

[[nodiscard]] constexpr IsaFamily isa_family_of(IsaTag tag) noexcept {
    const std::uint16_t ordinal = std::to_underlying(tag);
    if (ordinal < 200) return IsaFamily::X86;
    if (ordinal < 300) return IsaFamily::Arm;
    if (ordinal < 400) return IsaFamily::CudaPtx;
    if (ordinal < 500) return IsaFamily::Amdgcn;
    if (ordinal < 600) return IsaFamily::Tpu;
    return IsaFamily::Trn;
}

[[nodiscard]] constexpr bool vendor_isa_consistent(VendorBackend backend, IsaTag tag) noexcept {
    const IsaFamily family = isa_family_of(tag);
    switch (backend) {
        case VendorBackend::CPU:
            return family == IsaFamily::X86 || family == IsaFamily::Arm;
        case VendorBackend::NV:
            return family == IsaFamily::CudaPtx;
        case VendorBackend::AMD:
            return family == IsaFamily::Amdgcn;
        case VendorBackend::TPU:
            return family == IsaFamily::Tpu;
        case VendorBackend::TRN:
            return family == IsaFamily::Trn;
        case VendorBackend::None:
        case VendorBackend::CER:
        case VendorBackend::Portable:
            // No backend, no enumerated ISA family, and every backend in
            // turn: none of the three decodes a vendor-specific ISA.
            return false;
        default:
            // A future backend enumerator has no ISA family yet.
            return false;
    }
}

template <VendorBackend V, IsaTag I>
inline constexpr bool vendor_isa_consistent_v = vendor_isa_consistent(V, I);

}  // namespace crucible::fixy::vendor

namespace crucible::fixy::grant::vendor {

namespace fv = ::crucible::fixy::vendor;

template <fv::VendorBackend V, fv::IsaTag I>
    requires fv::vendor_isa_consistent_v<V, I>
struct intrinsic final : grant_base {};

}  // namespace crucible::fixy::grant::vendor

namespace crucible::fixy::grant {

template <::crucible::fixy::vendor::VendorBackend V, ::crucible::fixy::vendor::IsaTag I>
    requires ::crucible::fixy::vendor::vendor_isa_consistent_v<V, I>
struct which_dim<vendor::intrinsic<V, I>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

}  // namespace crucible::fixy::grant

namespace crucible::fixy::vendor {

namespace gv = ::crucible::fixy::grant::vendor;

using sse2_intrinsic = gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>;
using avx2_intrinsic = gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>;
using avx512bw_intrinsic = gv::intrinsic<VendorBackend::CPU, IsaTag::AVX512BW>;
using neon_intrinsic = gv::intrinsic<VendorBackend::CPU, IsaTag::NEON>;
using sve_intrinsic = gv::intrinsic<VendorBackend::CPU, IsaTag::SVE>;
using ptx_sm90_intrinsic = gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM90>;
using amdgcn_intrinsic = gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX1100>;

}  // namespace crucible::fixy::vendor

namespace crucible::fixy::vendor::detail::v258_self_test {

namespace gv = ::crucible::fixy::grant::vendor;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(isa_family_of(IsaTag::SSE2) == IsaFamily::X86);
static_assert(isa_family_of(IsaTag::AMXTILE) == IsaFamily::X86);
static_assert(isa_family_of(IsaTag::NEON) == IsaFamily::Arm);
static_assert(isa_family_of(IsaTag::SVE2) == IsaFamily::Arm);
static_assert(isa_family_of(IsaTag::CUDA_PTX_SM89) == IsaFamily::CudaPtx);
static_assert(isa_family_of(IsaTag::CUDA_PTX_SM100) == IsaFamily::CudaPtx);
static_assert(isa_family_of(IsaTag::AMDGCN_GFX1100) == IsaFamily::Amdgcn);
static_assert(isa_family_of(IsaTag::AMDGCN_GFX942) == IsaFamily::Amdgcn);
static_assert(isa_family_of(IsaTag::TPU_TFLITE) == IsaFamily::Tpu);
static_assert(isa_family_of(IsaTag::TRN_NEFF) == IsaFamily::Trn);

static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::SSE2));
static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::AVX512BW));
static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::NEON));
static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::SVE));
static_assert(vendor_isa_consistent(VendorBackend::NV, IsaTag::CUDA_PTX_SM90));
static_assert(vendor_isa_consistent(VendorBackend::AMD, IsaTag::AMDGCN_GFX1100));
static_assert(vendor_isa_consistent(VendorBackend::TPU, IsaTag::TPU_TFLITE));
static_assert(vendor_isa_consistent(VendorBackend::TRN, IsaTag::TRN_NEFF));
static_assert(!vendor_isa_consistent(VendorBackend::NV, IsaTag::AVX2));
static_assert(!vendor_isa_consistent(VendorBackend::CPU, IsaTag::CUDA_PTX_SM90));
static_assert(!vendor_isa_consistent(VendorBackend::AMD, IsaTag::NEON));
static_assert(!vendor_isa_consistent(VendorBackend::TPU, IsaTag::AMDGCN_GFX942));
static_assert(!vendor_isa_consistent(VendorBackend::None, IsaTag::SSE2));
static_assert(!vendor_isa_consistent(VendorBackend::CER, IsaTag::SSE2));
static_assert(!vendor_isa_consistent(VendorBackend::Portable, IsaTag::AVX2));

static_assert(IsGrantTag<gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(IsGrantTag<gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM100>>);
static_assert(IsGrantTag<gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX942>>);
static_assert(which_dim_v<gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>> == D::HwInstruction);
static_assert(which_dim_v<gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM90>> == D::HwInstruction);
static_assert(which_dim_v<gv::intrinsic<VendorBackend::TPU, IsaTag::TPU_TFLITE>> == D::HwInstruction);

static_assert(sizeof(gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>) == 1);
static_assert(sizeof(gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM90>) == 1);
static_assert(sizeof(gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX1100>) == 1);

static_assert(
    !std::is_same_v<gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>, gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(!std::is_same_v<gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM89>,
                              gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM90>>);
static_assert(
    std::is_same_v<gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>, gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);

static_assert(!::crucible::fixy::grant::IsGrantTag_v<const gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(!::crucible::fixy::grant::IsGrantTag_v<gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM90>&>);

static_assert(std::is_same_v<sse2_intrinsic, gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>>);
static_assert(std::is_same_v<avx2_intrinsic, gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(std::is_same_v<avx512bw_intrinsic, gv::intrinsic<VendorBackend::CPU, IsaTag::AVX512BW>>);
static_assert(std::is_same_v<neon_intrinsic, gv::intrinsic<VendorBackend::CPU, IsaTag::NEON>>);
static_assert(std::is_same_v<sve_intrinsic, gv::intrinsic<VendorBackend::CPU, IsaTag::SVE>>);
static_assert(std::is_same_v<ptx_sm90_intrinsic, gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM90>>);
static_assert(std::is_same_v<amdgcn_intrinsic, gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX1100>>);
static_assert(IsGrantTag<sse2_intrinsic>);
static_assert(IsGrantTag<ptx_sm90_intrinsic>);
static_assert(IsGrantTag<amdgcn_intrinsic>);

// The locals below are non-constant so the calls are not folded at compile
// time.
inline void runtime_smoke_test() {
    VendorBackend cpu = VendorBackend::CPU;
    IsaTag avx = IsaTag::AVX2;
    [[maybe_unused]] IsaFamily fam = isa_family_of(avx);
    [[maybe_unused]] bool consistent = vendor_isa_consistent(cpu, avx);
    [[maybe_unused]] bool mismatch = vendor_isa_consistent(VendorBackend::NV, avx);

    [[maybe_unused]] avx2_intrinsic a{};
    [[maybe_unused]] ptx_sm90_intrinsic p{};
    [[maybe_unused]] amdgcn_intrinsic g{};
    [[maybe_unused]] gv::intrinsic<VendorBackend::TRN, IsaTag::TRN_NEFF> t{};
}

}  // namespace crucible::fixy::vendor::detail::v258_self_test
