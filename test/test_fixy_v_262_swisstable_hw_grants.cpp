// The control-byte probe declares, per compile-time SIMD arm, the vendor
// intrinsic and register-width grants the active build uses. This file
// pins that declaration surface for whichever arm is active: the grants
// are well formed, route to the expected axis, and the declared register
// width agrees with the real control-byte group width.

#include <crucible/SwissTable.h>

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Simd.h>
#include <crucible/fixy/Vendor.h>

#include <cstdint>
#include <utility>

namespace {

namespace sw = ::crucible::detail::swiss_hw;
namespace fg = ::crucible::fixy::grant;
namespace fs = ::crucible::fixy::simd;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(fg::IsGrantTag<sw::ActiveSimdWidth>, "the active simd::width grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<sw::ActiveSimdWidth> == D::SimdIsa,
              "the active simd::width grant must route to the SimdIsa axis.");

// The group width is in bytes and the width grant is in bits, so the
// pairing check multiplies by eight.
#if defined(__AVX512BW__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_512>);
static_assert(::crucible::detail::group_width() * 8u == std::to_underlying(fs::WidthBits::Bits512));
#elif defined(__AVX2__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_256>);
static_assert(::crucible::detail::group_width() * 8u == std::to_underlying(fs::WidthBits::Bits256));
#elif defined(__SSE2__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_128>);
static_assert(::crucible::detail::group_width() * 8u == std::to_underlying(fs::WidthBits::Bits128));
#elif defined(__aarch64__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_128>);
static_assert(::crucible::detail::group_width() * 8u == std::to_underlying(fs::WidthBits::Bits128));
#else
// The portable path has no vector register, so it declares a scalar width.
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_scalar>,
              "the portable fallback must declare width_scalar.");
#endif

// The portable arm declares no vendor intrinsic, so this block covers only
// the four real-SIMD arms.
#if defined(__AVX512BW__) || defined(__AVX2__) || defined(__SSE2__) || defined(__aarch64__)
static_assert(fg::IsGrantTag<sw::ActiveVendorIsa>, "the active vendor::intrinsic grant must be well-formed.");
static_assert(fg::which_dim_v<sw::ActiveVendorIsa> == D::HwInstruction,
              "the active vendor::intrinsic grant must route to the HwInstruction "
              "axis.");
#endif

}  // namespace

int main() {
    // Running a real probe odr-uses the machinery the grant declarations
    // describe, so this is an integration check and not a header-only fold.
    const std::size_t bytes = ::crucible::detail::group_width();
    if (bytes != 16 && bytes != 32 && bytes != 64) return 1;  // the 3 valid widths

    // Sized for the widest group any arm uses. A narrower arm reads a prefix.
    alignas(64) std::int8_t ctrl[64];
    for (std::size_t i = 0; i < 64; ++i)
        ctrl[i] = static_cast<std::int8_t>(i & 0x7F);  // H2 tags 0x00..0x7F
    ctrl[3] = ::crucible::detail::kEmpty;  // the sole empty slot

    const auto group = ::crucible::detail::CtrlGroup::load(ctrl);
    const auto empties = group.match_empty();
    const auto fives = group.match(static_cast<std::int8_t>(5));

    // kEmpty (0x80) is the only byte with bit 7 set → index 3 is the lone match.
    if (!static_cast<bool>(empties)) return 2;
    if (empties.lowest() != 3u) return 3;
    // H2 tag 5 is unique at index 5 within any group width.
    if (!static_cast<bool>(fives)) return 4;
    if (fives.lowest() != 5u) return 5;
    return 0;
}
