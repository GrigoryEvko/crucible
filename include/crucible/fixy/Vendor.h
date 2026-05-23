#pragma once

// ── crucible::fixy::vendor — vendor-ISA intrinsic grant surface (FIXY-V-258) ─
//
// The structured per-vendor ISA-family grant for Agent 11 §3.4.  Where
// V-257's `grant::hw::vendor_intrinsic<Backend, rationale>` records a
// FREE-FORM intrinsic mnemonic on the Representation axis ("this kernel
// uses NV intrinsic 'wgmma'"), V-258's `grant::vendor::intrinsic<V, I>`
// records a STRUCTURED ISA-family capability on the HwInstruction axis
// ("this kernel issues AVX2-family instructions on the CPU backend").
// The two are complementary, not redundant — different axes, different
// parameter shapes.
//
// ── IsaTag — per-vendor ISA family enumeration (uint16_t) ─────────────
//
//   x86 family       (100-1xx): SSE2 / SSE4_1 / AVX2 / AVX512F / AVX512BW
//                               / BMI2 / AESNI / SHANI / AMXTILE
//   ARM family       (200-2xx): NEON / NEON_DotProd / NEON_FP16 / SVE / SVE2
//   CUDA PTX family  (300-3xx): SM89 / SM90 / SM100
//   AMDGCN family    (400-4xx): GFX1100 / GFX942
//   TPU family       (500):     TFLITE
//   Trainium family  (600):     NEFF
//
// The 100-unit family stride keeps `isa_family_of()` a single
// integer-division-free range check and leaves room to append within
// each family (append-only per FOUND-I04 — never renumber).
//
// ── intrinsic<V, I> — strong-typed vendor×ISA grant ───────────────────
//
//   template <VendorBackend V, IsaTag I>
//       requires vendor_isa_consistent_v<V, I>
//   struct intrinsic final : grant_base {};
//
// The `requires` clause is the STRONG-TYPING gate: `intrinsic<NV, AVX2>`
// (an NVIDIA GPU issuing x86 AVX2) is ill-formed at the template-id —
// NVIDIA silicon cannot decode x86 instructions.  The vendor↔family map:
//
//   CPU  ↔ {x86, ARM}   (CPU is x86_64 OR aarch64 host)
//   NV   ↔ CUDA PTX
//   AMD  ↔ AMDGCN
//   TPU  ↔ TPU
//   TRN  ↔ Trainium
//   None / CER / Portable ↔ ∅  (no IsaTag family — an intrinsic pinned
//                               to "no backend" / "every backend" is a
//                               contradiction)
//
// This is INTRA-grant well-formedness.  It is ORTHOGONAL to V-260's V001
// collision rule, which operates at the COMPOSITION layer: V001 states
// `intrinsic<V, I> ⇒ vendor_backend<V> engaged`, so a binding that
// engages two intrinsics with DIFFERENT V triggers two conflicting
// vendor_backend engagements → cross-vendor composition reject.  V-258
// ships the per-grant consistency; V-260 ships the cross-axis implication
// + cross-vendor composition reject.
//
// ── which_dim routing ─────────────────────────────────────────────────
//
// `which_dim<intrinsic<V, I>> = DimensionAxis::HwInstruction` — issuing a
// vendor intrinsic IS a hardware-instruction-class claim.  (The vendor
// identity surfaces separately via the V-260 V001 axis implication onto
// the Representation-axis Vendor wrapper; it is NOT this grant's
// which_dim.)  Per Grant.h's namespace-purity discipline (CR-09), the
// which_dim specialization reopens `crucible::fixy::grant`; this header is
// allowlisted in scripts/check-fixy-grant-namespace-purity.sh alongside
// Hw.h / Fp.h / Fs.h.
//
// ── Consumers ─────────────────────────────────────────────────────────
//
//   V-262 SwissTable.h #if arms       — declare vendor::sse2/avx2/avx512bw
//   V-263 cntp/Fec.h #if arms          — declare vendor::avx2/neon intrinsics
//   V-266 mimic per-vendor backends    — emit_kernel returns Vendor<V, ...>
//                                        with the IsaTag the kernel targets
//
// ── Axiom coverage (CLAUDE.md §II) ────────────────────────────────────
//
//   InitSafe   — `intrinsic` is a `final` empty struct; IsaTag has explicit
//                ordinals; isa_family_of is total over the enum.
//   TypeSafe   — strong scoped enums (IsaTag : uint16_t, IsaFamily); the
//                vendor_isa_consistent gate forbids nonsensical pairs.
//   NullSafe   — no pointer surface.
//   MemSafe / BorrowSafe / ThreadSafe / LeakSafe — zero-state grant tag.
//   DetSafe    — same (V, I) → same grant type on any platform.
//
// ── HS14 fixtures (cross-vendor mismatch, test/fixy_neg/) ─────────────
//
//   neg_fixy_v_258_intrinsic_nv_x86   — intrinsic<NV, AVX2>  (NV + x86)
//   neg_fixy_v_258_intrinsic_cpu_ptx  — intrinsic<CPU, CUDA_PTX_SM90> (CPU + NV ISA)
//   neg_fixy_v_258_intrinsic_amd_neon — intrinsic<AMD, NEON> (AMD + ARM ISA)

#include <crucible/fixy/Grant.h>            // grant_base, which_dim, IsGrantTag
#include <crucible/fixy/Dim.h>              // dim::DimensionAxis
#include <crucible/safety/Vendor.h>         // VendorBackend_v

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::fixy::vendor {

// Re-export of the V-250-lineage VendorBackend enum into the vendor
// namespace so `intrinsic<V, I>` and the aliases cite one spelling.
using VendorBackend = ::crucible::safety::VendorBackend_v;

// ═════════════════════════════════════════════════════════════════════
// ── IsaTag — per-vendor ISA family enumeration ────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// 100-unit family stride; append within a family at the next free
// ordinal, NEVER renumber (FOUND-I04 append-only — a stored federation
// row_hash keys on the ordinal).
enum class IsaTag : std::uint16_t {
    // ── x86 family (100-1xx) ──
    SSE2     = 100,
    SSE4_1   = 101,
    AVX2     = 102,
    AVX512F  = 103,
    AVX512BW = 104,
    BMI2     = 110,
    AESNI    = 111,
    SHANI    = 112,
    AMXTILE  = 113,
    // ── ARM family (200-2xx) ──
    NEON         = 200,
    NEON_DotProd = 201,
    NEON_FP16    = 202,
    SVE          = 203,
    SVE2         = 204,
    // ── CUDA PTX family (300-3xx) ──
    CUDA_PTX_SM89  = 300,
    CUDA_PTX_SM90  = 301,
    CUDA_PTX_SM100 = 302,
    // ── AMDGCN family (400-4xx) ──
    AMDGCN_GFX1100 = 400,
    AMDGCN_GFX942  = 401,
    // ── TPU family (500) ──
    TPU_TFLITE = 500,
    // ── Trainium family (600) ──
    TRN_NEFF = 600,
};

// ── ISA family classification — the vendor-consistency join key ───────
enum class IsaFamily : std::uint8_t { X86, Arm, CudaPtx, Amdgcn, Tpu, Trn };

[[nodiscard]] constexpr IsaFamily isa_family_of(IsaTag tag) noexcept {
    const std::uint16_t ordinal = std::to_underlying(tag);
    if (ordinal < 200) return IsaFamily::X86;      // 100-1xx
    if (ordinal < 300) return IsaFamily::Arm;      // 200-2xx
    if (ordinal < 400) return IsaFamily::CudaPtx;  // 300-3xx
    if (ordinal < 500) return IsaFamily::Amdgcn;   // 400-4xx
    if (ordinal < 600) return IsaFamily::Tpu;      // 500-5xx
    return IsaFamily::Trn;                          // 600+
}

// ── vendor_isa_consistent — the strong-typing gate ────────────────────
//
// True iff backend V can actually decode IsaTag I's instruction family.
[[nodiscard]] constexpr bool vendor_isa_consistent(VendorBackend backend,
                                                   IsaTag tag) noexcept {
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
            // None (no kernel), CER (no IsaTag family enumerated yet), and
            // Portable (vendor-agnostic) admit NO vendor-specific intrinsic.
            return false;
        default:
            // A future VendorBackend enumerator with no IsaTag family yet.
            return false;
    }
}

template <VendorBackend V, IsaTag I>
inline constexpr bool vendor_isa_consistent_v = vendor_isa_consistent(V, I);

}  // namespace crucible::fixy::vendor

// ═════════════════════════════════════════════════════════════════════
// ── intrinsic grant (crucible::fixy::grant::vendor) ───────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::vendor {

namespace fv = ::crucible::fixy::vendor;

// intrinsic<V, I> — strong-typed vendor×ISA grant.  The requires-clause
// makes a vendor↔family mismatch (e.g. intrinsic<NV, AVX2>) ill-formed at
// the template-id.  EBO-collapsible (sizeof == 1 standalone, 0 inside an
// aggregator).
template <fv::VendorBackend V, fv::IsaTag I>
    requires fv::vendor_isa_consistent_v<V, I>
struct intrinsic final : grant_base {};

}  // namespace crucible::fixy::grant::vendor

// ── which_dim routing — CR-09 locked namespace ───────────────────────

namespace crucible::fixy::grant {

template <::crucible::fixy::vendor::VendorBackend V, ::crucible::fixy::vendor::IsaTag I>
    requires ::crucible::fixy::vendor::vendor_isa_consistent_v<V, I>
struct which_dim<vendor::intrinsic<V, I>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::HwInstruction> {};

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── 7 canonical aliases (crucible::fixy::vendor) ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::vendor {

namespace gv = ::crucible::fixy::grant::vendor;

using sse2_intrinsic     = gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>;
using avx2_intrinsic     = gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>;
using avx512bw_intrinsic = gv::intrinsic<VendorBackend::CPU, IsaTag::AVX512BW>;
using neon_intrinsic     = gv::intrinsic<VendorBackend::CPU, IsaTag::NEON>;
using sve_intrinsic      = gv::intrinsic<VendorBackend::CPU, IsaTag::SVE>;
using ptx_sm90_intrinsic = gv::intrinsic<VendorBackend::NV,  IsaTag::CUDA_PTX_SM90>;
using amdgcn_intrinsic   = gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX1100>;

}  // namespace crucible::fixy::vendor

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::vendor::detail::v258_self_test {

namespace gv  = ::crucible::fixy::grant::vendor;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── Layer 1: isa_family_of total + correct at every boundary ──────────
static_assert(isa_family_of(IsaTag::SSE2)           == IsaFamily::X86);
static_assert(isa_family_of(IsaTag::AMXTILE)        == IsaFamily::X86);
static_assert(isa_family_of(IsaTag::NEON)           == IsaFamily::Arm);
static_assert(isa_family_of(IsaTag::SVE2)           == IsaFamily::Arm);
static_assert(isa_family_of(IsaTag::CUDA_PTX_SM89)  == IsaFamily::CudaPtx);
static_assert(isa_family_of(IsaTag::CUDA_PTX_SM100) == IsaFamily::CudaPtx);
static_assert(isa_family_of(IsaTag::AMDGCN_GFX1100) == IsaFamily::Amdgcn);
static_assert(isa_family_of(IsaTag::AMDGCN_GFX942)  == IsaFamily::Amdgcn);
static_assert(isa_family_of(IsaTag::TPU_TFLITE)     == IsaFamily::Tpu);
static_assert(isa_family_of(IsaTag::TRN_NEFF)       == IsaFamily::Trn);

// ── Layer 2: vendor_isa_consistent — the strong-typing gate ───────────
// Positive: every canonical alias's (V, I) pair is consistent.
static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::SSE2));
static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::AVX512BW));
static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::NEON));   // CPU = x86 OR ARM
static_assert(vendor_isa_consistent(VendorBackend::CPU, IsaTag::SVE));
static_assert(vendor_isa_consistent(VendorBackend::NV,  IsaTag::CUDA_PTX_SM90));
static_assert(vendor_isa_consistent(VendorBackend::AMD, IsaTag::AMDGCN_GFX1100));
static_assert(vendor_isa_consistent(VendorBackend::TPU, IsaTag::TPU_TFLITE));
static_assert(vendor_isa_consistent(VendorBackend::TRN, IsaTag::TRN_NEFF));
// Negative: cross-vendor pairs + the no-family backends.
static_assert(!vendor_isa_consistent(VendorBackend::NV,  IsaTag::AVX2));         // NV ✗ x86
static_assert(!vendor_isa_consistent(VendorBackend::CPU, IsaTag::CUDA_PTX_SM90)); // CPU ✗ PTX
static_assert(!vendor_isa_consistent(VendorBackend::AMD, IsaTag::NEON));         // AMD ✗ ARM
static_assert(!vendor_isa_consistent(VendorBackend::TPU, IsaTag::AMDGCN_GFX942)); // TPU ✗ GCN
static_assert(!vendor_isa_consistent(VendorBackend::None,     IsaTag::SSE2));
static_assert(!vendor_isa_consistent(VendorBackend::CER,      IsaTag::SSE2));
static_assert(!vendor_isa_consistent(VendorBackend::Portable, IsaTag::AVX2));

// ── Layer 3: intrinsic is a valid grant tag, routes to HwInstruction ──
static_assert(IsGrantTag<gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(IsGrantTag<gv::intrinsic<VendorBackend::NV,  IsaTag::CUDA_PTX_SM100>>);
static_assert(IsGrantTag<gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX942>>);
static_assert(which_dim_v<gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>> == D::HwInstruction);
static_assert(which_dim_v<gv::intrinsic<VendorBackend::NV,  IsaTag::CUDA_PTX_SM90>> == D::HwInstruction);
static_assert(which_dim_v<gv::intrinsic<VendorBackend::TPU, IsaTag::TPU_TFLITE>>    == D::HwInstruction);

// ── Layer 4: sizeof — EBO-collapsible (1 byte standalone) ─────────────
static_assert(sizeof(gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>)      == 1);
static_assert(sizeof(gv::intrinsic<VendorBackend::NV,  IsaTag::CUDA_PTX_SM90>) == 1);
static_assert(sizeof(gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX1100>) == 1);

// ── Layer 5: NTTP distinctness — different V or different I → different type ─
static_assert(!std::is_same_v<gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>,
                              gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(!std::is_same_v<gv::intrinsic<VendorBackend::NV,  IsaTag::CUDA_PTX_SM89>,
                              gv::intrinsic<VendorBackend::NV,  IsaTag::CUDA_PTX_SM90>>);
static_assert( std::is_same_v<gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>,
                              gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);

// ── Layer 6: cv-ref rejection (fixy-A4-033) ───────────────────────────
static_assert(!::crucible::fixy::grant::IsGrantTag_v<const gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(!::crucible::fixy::grant::IsGrantTag_v<gv::intrinsic<VendorBackend::NV, IsaTag::CUDA_PTX_SM90>&>);

// ── Layer 7: the 7 canonical aliases resolve to the documented pairs ──
static_assert(std::is_same_v<sse2_intrinsic,     gv::intrinsic<VendorBackend::CPU, IsaTag::SSE2>>);
static_assert(std::is_same_v<avx2_intrinsic,     gv::intrinsic<VendorBackend::CPU, IsaTag::AVX2>>);
static_assert(std::is_same_v<avx512bw_intrinsic, gv::intrinsic<VendorBackend::CPU, IsaTag::AVX512BW>>);
static_assert(std::is_same_v<neon_intrinsic,     gv::intrinsic<VendorBackend::CPU, IsaTag::NEON>>);
static_assert(std::is_same_v<sve_intrinsic,      gv::intrinsic<VendorBackend::CPU, IsaTag::SVE>>);
static_assert(std::is_same_v<ptx_sm90_intrinsic, gv::intrinsic<VendorBackend::NV,  IsaTag::CUDA_PTX_SM90>>);
static_assert(std::is_same_v<amdgcn_intrinsic,   gv::intrinsic<VendorBackend::AMD, IsaTag::AMDGCN_GFX1100>>);
static_assert(IsGrantTag<sse2_intrinsic>);
static_assert(IsGrantTag<ptx_sm90_intrinsic>);
static_assert(IsGrantTag<amdgcn_intrinsic>);

// ── Runtime smoke test — non-constant args defeat consteval folding ───
inline void runtime_smoke_test() {
    VendorBackend cpu = VendorBackend::CPU;
    IsaTag        avx = IsaTag::AVX2;
    [[maybe_unused]] IsaFamily fam       = isa_family_of(avx);
    [[maybe_unused]] bool      consistent = vendor_isa_consistent(cpu, avx);
    [[maybe_unused]] bool      mismatch   = vendor_isa_consistent(VendorBackend::NV, avx);

    // Grant + alias round-trip at runtime.
    [[maybe_unused]] avx2_intrinsic     a{};
    [[maybe_unused]] ptx_sm90_intrinsic p{};
    [[maybe_unused]] amdgcn_intrinsic   g{};
    [[maybe_unused]] gv::intrinsic<VendorBackend::TRN, IsaTag::TRN_NEFF> t{};
}

}  // namespace crucible::fixy::vendor::detail::v258_self_test
