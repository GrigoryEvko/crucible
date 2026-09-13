// Sentinel TU: compiles the header under the project warning flags so its
// static_asserts run.

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

static_assert(fg::IsGrantTag<fh::ActiveSimdWidth>, "the active simd::width grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<fh::ActiveSimdWidth> == D::SimdIsa,
              "the active simd::width grant must route to the SimdIsa axis.");

// The 32 and 16 byte literals below are the kernel block strides.  Each must
// match the register width the arm declares.
#if defined(__AVX2__)
static_assert(std::is_same_v<fh::ActiveSimdWidth, fs::width_256>);
static_assert(32u * 8u == std::to_underlying(fs::WidthBits::Bits256));
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
static_assert(std::is_same_v<fh::ActiveSimdWidth, fs::width_128>);
static_assert(16u * 8u == std::to_underlying(fs::WidthBits::Bits128));
#else
static_assert(std::is_same_v<fh::ActiveSimdWidth, fs::width_scalar>,
              "the portable scalar FEC fallback must declare width_scalar.");
#endif

#if defined(__AVX2__) || ((defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__))
static_assert(fg::IsGrantTag<fh::ActiveVendorIsa>, "the active vendor::intrinsic grant must be well-formed.");
static_assert(fg::which_dim_v<fh::ActiveVendorIsa> == D::HwInstruction,
              "the active vendor::intrinsic grant must route to the HwInstruction axis.");
#endif

}  // namespace

int main() {
    // The encode round-trip ODR-uses the active arm's kernel, so the grant
    // declarations are checked against code that actually runs.
    namespace ci = ::crucible::cntp;
    ci::ReedSolomon<4, 2> codec{};

    constexpr std::size_t kPayload = 64;
    std::array<std::byte, kPayload> payload{};
    for (std::size_t i = 0; i < kPayload; ++i)
        payload[i] = static_cast<std::byte>(i & 0xFFU);

    std::array<std::byte, ci::ReedSolomon<4, 2>::encoded_size_for(kPayload)> encoded{};
    const auto enc = codec.encode(std::span<const std::byte>{payload}, encoded);
    if (!enc.has_value()) return 1;
    return 0;
}
