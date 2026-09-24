// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags.

#include <crucible/fixy/Vendor.h>

#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace fv = ::crucible::fixy::vendor;
namespace gv = ::crucible::fixy::grant::vendor;
namespace gr = ::crucible::fixy::grant;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(std::meta::enumerators_of(^^fv::IsaTag).size() == 21,
              "IsaTag diverged from the 21-family roster (x86 9 + ARM 5 + CUDA 3 + "
              "AMDGCN 2 + TPU 1 + Trainium 1). Append within a family at the next "
              "free ordinal and never renumber.");

// The family base ordinals are frozen. A new member appends inside its
// family, so a consumer keying off a base ordinal keeps working.
static_assert(std::to_underlying(fv::IsaTag::SSE2) == 100);
static_assert(std::to_underlying(fv::IsaTag::AVX2) == 102);
static_assert(std::to_underlying(fv::IsaTag::AVX512BW) == 104);
static_assert(std::to_underlying(fv::IsaTag::BMI2) == 110);
static_assert(std::to_underlying(fv::IsaTag::NEON) == 200);
static_assert(std::to_underlying(fv::IsaTag::SVE) == 203);
static_assert(std::to_underlying(fv::IsaTag::CUDA_PTX_SM89) == 300);
static_assert(std::to_underlying(fv::IsaTag::AMDGCN_GFX1100) == 400);
static_assert(std::to_underlying(fv::IsaTag::TPU_TFLITE) == 500);
static_assert(std::to_underlying(fv::IsaTag::TRN_NEFF) == 600);

// Every backend enumerator gets one line, so the gate is exercised as a
// total function rather than through its fallthrough.
static_assert(fv::vendor_isa_consistent(fv::VendorBackend::CPU, fv::IsaTag::AVX2));
static_assert(fv::vendor_isa_consistent(fv::VendorBackend::NV, fv::IsaTag::CUDA_PTX_SM90));
static_assert(fv::vendor_isa_consistent(fv::VendorBackend::AMD, fv::IsaTag::AMDGCN_GFX942));
static_assert(fv::vendor_isa_consistent(fv::VendorBackend::TPU, fv::IsaTag::TPU_TFLITE));
static_assert(fv::vendor_isa_consistent(fv::VendorBackend::TRN, fv::IsaTag::TRN_NEFF));
static_assert(!fv::vendor_isa_consistent(fv::VendorBackend::None, fv::IsaTag::SSE2));
static_assert(!fv::vendor_isa_consistent(fv::VendorBackend::CER, fv::IsaTag::SSE2));
static_assert(!fv::vendor_isa_consistent(fv::VendorBackend::Portable, fv::IsaTag::SSE2));

// The grant routes to the instruction axis, not the representation axis.
// Vendor identity reaches the representation axis by a separate rule.
static_assert(gr::which_dim_v<gv::intrinsic<fv::VendorBackend::CPU, fv::IsaTag::AVX512BW>> == D::HwInstruction);
static_assert(gr::which_dim_v<gv::intrinsic<fv::VendorBackend::NV, fv::IsaTag::CUDA_PTX_SM100>> == D::HwInstruction);

static_assert(gr::IsGrantTag<fv::sse2_intrinsic>);
static_assert(gr::IsGrantTag<fv::avx2_intrinsic>);
static_assert(gr::IsGrantTag<fv::avx512bw_intrinsic>);
static_assert(gr::IsGrantTag<fv::neon_intrinsic>);
static_assert(gr::IsGrantTag<fv::sve_intrinsic>);
static_assert(gr::IsGrantTag<fv::ptx_sm90_intrinsic>);
static_assert(gr::IsGrantTag<fv::amdgcn_intrinsic>);

static_assert(gr::IsGrantTag<gv::intrinsic<fv::VendorBackend::CPU, fv::IsaTag::AVX2>>);  // x86
static_assert(gr::IsGrantTag<gv::intrinsic<fv::VendorBackend::CPU, fv::IsaTag::SVE2>>);  // ARM

}  // namespace

int main() {
    ::crucible::fixy::vendor::detail::v258_self_test::runtime_smoke_test();
    return 0;
}
