// FIXY-V-258 sentinel TU: fixy/Vendor.h — IsaTag enum (22 per-vendor ISA
// families) + the strong-typed `grant::vendor::intrinsic<V, I>` grant
// routing onto DimensionAxis::HwInstruction, plus 7 canonical aliases.
//
// Forces every header-embedded static_assert under the project warning
// flags (header-only static_asserts are otherwise unverified —
// feedback_header_only_static_assert_blind_spot) and adds cross-cutting
// checks: IsaTag cardinality, the vendor×ISA consistency gate's
// completeness over the VendorBackend enum, and the runtime smoke test.
//
// HS14 cross-vendor mismatch coverage lives in three fixtures in
// test/fixy_neg/neg_fixy_v_258_intrinsic_*.cpp.

#include <crucible/fixy/Vendor.h>

#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace fv = ::crucible::fixy::vendor;
namespace gv = ::crucible::fixy::grant::vendor;
namespace gr = ::crucible::fixy::grant;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── IsaTag cardinality — 22 ISA families ship (reflection-derived) ────
static_assert(std::meta::enumerators_of(^^fv::IsaTag).size() == 21,
    "IsaTag diverged from the 21-family roster (x86 9 + ARM 5 + CUDA 3 + "
    "AMDGCN 2 + TPU 1 + Trainium 1).  Append within a family at the next "
    "free ordinal (FOUND-I04 append-only); never renumber.");

// ── Family-stride pins — the documented base ordinals are stable ──────
static_assert(std::to_underlying(fv::IsaTag::SSE2)           == 100);
static_assert(std::to_underlying(fv::IsaTag::AVX2)           == 102);
static_assert(std::to_underlying(fv::IsaTag::AVX512BW)       == 104);
static_assert(std::to_underlying(fv::IsaTag::BMI2)           == 110);
static_assert(std::to_underlying(fv::IsaTag::NEON)           == 200);
static_assert(std::to_underlying(fv::IsaTag::SVE)            == 203);
static_assert(std::to_underlying(fv::IsaTag::CUDA_PTX_SM89)  == 300);
static_assert(std::to_underlying(fv::IsaTag::AMDGCN_GFX1100) == 400);
static_assert(std::to_underlying(fv::IsaTag::TPU_TFLITE)     == 500);
static_assert(std::to_underlying(fv::IsaTag::TRN_NEFF)       == 600);

// ── vendor_isa_consistent is total over the VendorBackend enum ────────
// Every backend value resolves the gate without hitting the fallthrough
// `return false` ambiguously — exercise each enumerator with one ISA.
static_assert( fv::vendor_isa_consistent(fv::VendorBackend::CPU,      fv::IsaTag::AVX2));
static_assert( fv::vendor_isa_consistent(fv::VendorBackend::NV,       fv::IsaTag::CUDA_PTX_SM90));
static_assert( fv::vendor_isa_consistent(fv::VendorBackend::AMD,      fv::IsaTag::AMDGCN_GFX942));
static_assert( fv::vendor_isa_consistent(fv::VendorBackend::TPU,      fv::IsaTag::TPU_TFLITE));
static_assert( fv::vendor_isa_consistent(fv::VendorBackend::TRN,      fv::IsaTag::TRN_NEFF));
static_assert(!fv::vendor_isa_consistent(fv::VendorBackend::None,     fv::IsaTag::SSE2));
static_assert(!fv::vendor_isa_consistent(fv::VendorBackend::CER,      fv::IsaTag::SSE2));
static_assert(!fv::vendor_isa_consistent(fv::VendorBackend::Portable, fv::IsaTag::SSE2));

// ── The grant routes to HwInstruction, NOT Representation ─────────────
// (the vendor identity surfaces via the V-260 V001 axis implication onto
// the Representation-axis Vendor wrapper — separate from this which_dim).
static_assert(gr::which_dim_v<gv::intrinsic<fv::VendorBackend::CPU, fv::IsaTag::AVX512BW>>
              == D::HwInstruction);
static_assert(gr::which_dim_v<gv::intrinsic<fv::VendorBackend::NV, fv::IsaTag::CUDA_PTX_SM100>>
              == D::HwInstruction);

// ── The 7 canonical aliases are all consistent grant tags ─────────────
static_assert(gr::IsGrantTag<fv::sse2_intrinsic>);
static_assert(gr::IsGrantTag<fv::avx2_intrinsic>);
static_assert(gr::IsGrantTag<fv::avx512bw_intrinsic>);
static_assert(gr::IsGrantTag<fv::neon_intrinsic>);
static_assert(gr::IsGrantTag<fv::sve_intrinsic>);
static_assert(gr::IsGrantTag<fv::ptx_sm90_intrinsic>);
static_assert(gr::IsGrantTag<fv::amdgcn_intrinsic>);

// ── Cross-family CPU duality witness — CPU admits BOTH x86 AND ARM ────
static_assert(gr::IsGrantTag<gv::intrinsic<fv::VendorBackend::CPU, fv::IsaTag::AVX2>>);  // x86
static_assert(gr::IsGrantTag<gv::intrinsic<fv::VendorBackend::CPU, fv::IsaTag::SVE2>>);  // ARM

}  // namespace

int main() {
    ::crucible::fixy::vendor::detail::v258_self_test::runtime_smoke_test();
    return 0;
}
