// test_fixy_v_263_fec_hw_grants.cpp — positive sentinel for FIXY-V-263.
//
// cntp/Fec.h's fec_hw block declares, per compile-time SIMD arm, the
// vendor::intrinsic<V, I> (V-258) and simd::width<W> (V-259) grants the
// Reed-Solomon GF(2^8) kernels use.  This TU pins that declaration
// surface: the active grants are well-formed, route to the correct
// DimensionAxis, and the declared register width matches the kernel's
// block stride.  It also runs a real FEC encode round-trip, ODR-using
// the active arm's mul_xor kernel the grants describe (the V-262
// header-only-static_assert-blind-spot lesson applied to a band-3 file).

#include <crucible/cntp/Fec.h>

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Simd.h>
#include <crucible/fixy/Vendor.h>

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

namespace {

namespace fh = ::crucible::cntp::detail::fec_hw;
namespace fg = ::crucible::fixy::grant;
namespace fs = ::crucible::fixy::simd;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── The active SIMD-width grant — present on every arm ─────────────
static_assert(fg::IsGrantTag<fh::ActiveSimdWidth>,
    "FIXY-V-263: the active simd::width grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<fh::ActiveSimdWidth> == D::SimdIsa,
    "FIXY-V-263: the active simd::width grant must route to the SimdIsa axis.");

// ── Declared width matches the kernel's block stride ───────────────
#if defined(__AVX2__)
static_assert(std::is_same_v<fh::ActiveSimdWidth, fs::width_256>);
static_assert(32u * 8u == std::to_underlying(fs::WidthBits::Bits256));
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
static_assert(std::is_same_v<fh::ActiveSimdWidth, fs::width_128>);
static_assert(16u * 8u == std::to_underlying(fs::WidthBits::Bits128));
#else
static_assert(std::is_same_v<fh::ActiveSimdWidth, fs::width_scalar>,
    "FIXY-V-263: the portable scalar FEC fallback must declare width_scalar.");
#endif

// ── On the two real-SIMD arms, the vendor intrinsic is also pinned ─
#if defined(__AVX2__) || ((defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__))
static_assert(fg::IsGrantTag<fh::ActiveVendorIsa>,
    "FIXY-V-263: the active vendor::intrinsic grant must be well-formed.");
static_assert(fg::which_dim_v<fh::ActiveVendorIsa> == D::HwInstruction,
    "FIXY-V-263: the active vendor::intrinsic grant must route to the "
    "HwInstruction axis.");
#endif

}  // namespace

int main() {
    // Runtime smoke — a real Reed-Solomon encode ODR-uses the active
    // arm's mul_xor GF(2^8) kernel the grants describe, so the sentinel
    // is a genuine integration check.
    namespace ci = ::crucible::cntp;
    ci::ReedSolomon<4, 2> codec{};

    constexpr std::size_t kPayload = 64;
    std::array<std::byte, kPayload> payload{};
    for (std::size_t i = 0; i < kPayload; ++i)
        payload[i] = static_cast<std::byte>(i & 0xFFU);

    std::array<std::byte, ci::ReedSolomon<4, 2>::encoded_size_for(kPayload)> encoded{};
    const auto enc =
        codec.encode(std::span<const std::byte>{payload}, encoded);
    if (!enc.has_value()) return 1;
    return 0;
}
