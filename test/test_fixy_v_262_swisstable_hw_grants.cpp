// test_fixy_v_262_swisstable_hw_grants.cpp — positive sentinel for FIXY-V-262.
//
// SwissTable.h's swiss_hw block declares, per compile-time SIMD arm, the
// vendor::intrinsic<V, I> (V-258) and simd::width<W> (V-259) grants the
// active build uses for the control-byte probe.  This TU pins that
// declaration surface: the active grants are well-formed, route to the
// correct DimensionAxis, and the declared register width matches the
// actual control-byte group width.  Per the header-only static_assert
// blind-spot discipline, the in-header asserts are re-witnessed here in a
// dedicated consumer TU compiled under the project warning flags.

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

// ── The active SIMD-width grant — always present on every arm ──────
static_assert(fg::IsGrantTag<sw::ActiveSimdWidth>,
    "FIXY-V-262: the active simd::width grant must be a well-formed grant tag.");
static_assert(fg::which_dim_v<sw::ActiveSimdWidth> == D::SimdIsa,
    "FIXY-V-262: the active simd::width grant must route to the SimdIsa axis.");

// ── The declared register width matches the control-byte group width ─
// group_width() (bytes) × 8 == the declared width grant's bit count.
// We re-witness the active arm's pairing against the WidthBits enum.
#if defined(__AVX512BW__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_512>);
static_assert(::crucible::detail::group_width() * 8u
                  == std::to_underlying(fs::WidthBits::Bits512));
#elif defined(__AVX2__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_256>);
static_assert(::crucible::detail::group_width() * 8u
                  == std::to_underlying(fs::WidthBits::Bits256));
#elif defined(__SSE2__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_128>);
static_assert(::crucible::detail::group_width() * 8u
                  == std::to_underlying(fs::WidthBits::Bits128));
#elif defined(__aarch64__)
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_128>);
static_assert(::crucible::detail::group_width() * 8u
                  == std::to_underlying(fs::WidthBits::Bits128));
#else
// Portable SWAR: no SIMD vector register, so width_scalar.
static_assert(std::is_same_v<sw::ActiveSimdWidth, fs::width_scalar>,
    "FIXY-V-262: the portable SWAR fallback must declare width_scalar.");
#endif

// ── On the four real-SIMD arms, the vendor intrinsic is also pinned ─
#if defined(__AVX512BW__) || defined(__AVX2__) || defined(__SSE2__) || defined(__aarch64__)
static_assert(fg::IsGrantTag<sw::ActiveVendorIsa>,
    "FIXY-V-262: the active vendor::intrinsic grant must be well-formed.");
static_assert(fg::which_dim_v<sw::ActiveVendorIsa> == D::HwInstruction,
    "FIXY-V-262: the active vendor::intrinsic grant must route to the "
    "HwInstruction axis.");
#endif

}  // namespace

int main() {
    // Runtime smoke — run a real control-byte probe with the active arm's
    // declared width.  This ODR-uses the full SwissTable machinery
    // (kEmpty, CtrlGroup, BitMask) the grant declarations describe, so the
    // sentinel is a genuine integration check, not a header-only fold.
    const std::size_t bytes = ::crucible::detail::group_width();
    if (bytes != 16 && bytes != 32 && bytes != 64) return 1;  // the 3 valid widths

    // 64 bytes covers the widest (AVX-512) group; narrower arms read a prefix.
    alignas(64) std::int8_t ctrl[64];
    for (std::size_t i = 0; i < 64; ++i)
        ctrl[i] = static_cast<std::int8_t>(i & 0x7F);  // H2 tags 0x00..0x7F
    ctrl[3] = ::crucible::detail::kEmpty;              // the sole empty slot

    const auto group   = ::crucible::detail::CtrlGroup::load(ctrl);
    const auto empties = group.match_empty();           // exercises kEmpty
    const auto fives   = group.match(static_cast<std::int8_t>(5));

    // kEmpty (0x80) is the only byte with bit 7 set → index 3 is the lone match.
    if (!static_cast<bool>(empties)) return 2;
    if (empties.lowest() != 3u) return 3;
    // H2 tag 5 is unique at index 5 within any group width.
    if (!static_cast<bool>(fives)) return 4;
    if (fives.lowest() != 5u) return 5;
    return 0;
}
