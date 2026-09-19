#pragma once

#include <crucible/Platform.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/fixy/Vendor.h>
#include <crucible/fixy/Simd.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Pre.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

#if defined(__AVX512BW__)
#include <immintrin.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace crucible {
namespace detail {

static constexpr int8_t kEmpty = static_cast<int8_t>(0x80);

using GroupWidth = ::crucible::fixy::wrap::PowerOfTwo<std::size_t>;

#if defined(__AVX512BW__)
static constexpr GroupWidth kGroupWidth{std::size_t{64}};
#elif defined(__AVX2__)
static constexpr GroupWidth kGroupWidth{std::size_t{32}};
#else
static constexpr GroupWidth kGroupWidth{std::size_t{16}};
#endif

static_assert(sizeof(GroupWidth) == sizeof(std::size_t));
static_assert(std::is_trivially_copyable_v<GroupWidth>);
static_assert(std::is_standard_layout_v<GroupWidth>);

[[nodiscard]] consteval std::size_t group_width() noexcept { return kGroupWidth.value(); }

// The upper bound is 64 because BitMask carries the group in a uint64_t.
// A wider group needs a wider mask type and a re-audit of the H2 tag width.
static_assert(::crucible::decide::is_power_of_two_le<std::size_t>(group_width(), std::size_t{64}),
              "kGroupWidth must be a power of two ≤ 64 (AVX-512 width)");

namespace swiss_hw {

namespace fv = ::crucible::fixy::vendor;
namespace fs = ::crucible::fixy::simd;

#if defined(__AVX512BW__)
using ActiveVendorIsa = fv::avx512bw_intrinsic;
using ActiveSimdWidth = fs::width_512;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits512),
              "AVX-512BW group bytes x 8 must equal width_512 bits");
#elif defined(__AVX2__)
using ActiveVendorIsa = fv::avx2_intrinsic;
using ActiveSimdWidth = fs::width_256;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits256),
              "AVX2 group bytes x 8 must equal width_256 bits");
#elif defined(__SSE2__)
using ActiveVendorIsa = fv::sse2_intrinsic;
using ActiveSimdWidth = fs::width_128;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits128),
              "SSE2 group bytes x 8 must equal width_128 bits");
#elif defined(__aarch64__)
using ActiveVendorIsa = fv::neon_intrinsic;
using ActiveSimdWidth = fs::width_128;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits128),
              "NEON group bytes x 8 must equal width_128 bits");
#else
using ActiveSimdWidth = fs::width_scalar;
#endif

static_assert(::crucible::fixy::grant::IsGrantTag<ActiveSimdWidth>, "the active simd::width grant must be well-formed");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveSimdWidth> == ::crucible::fixy::dim::DimensionAxis::SimdIsa,
              "simd::width routes to the SimdIsa axis");

// The portable arm runs SWAR over general-purpose registers, so it declares
// no vendor intrinsic. Only the four vector arms pin one.
#if defined(__AVX512BW__) || defined(__AVX2__) || defined(__SSE2__) || defined(__aarch64__)
static_assert(::crucible::fixy::grant::IsGrantTag<ActiveVendorIsa>,
              "the active vendor::intrinsic grant must be well-formed");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveVendorIsa>
                  == ::crucible::fixy::dim::DimensionAxis::HwInstruction,
              "vendor::intrinsic routes to the HwInstruction axis");
#endif

}  // namespace swiss_hw

[[nodiscard]] consteval uint64_t group_mask_ceiling(std::size_t width) noexcept {
    return width == 64 ? std::numeric_limits<uint64_t>::max() : ((uint64_t{1} << width) - uint64_t{1});
}

static constexpr uint64_t kGroupMaskCeiling = group_mask_ceiling(group_width());

// The shift by 57 leaves the result in [0, 127], so bit 7 of the int8_t is
// always clear. That reserves 0x80 for kEmpty: a tag with bit 7 set would
// read as an empty slot and end the probe loop early.
//
// A post clause here is rejected. The constexpr evaluator folds the body to
// a constant and then reports the contract condition as non-constant. The
// boundary static_asserts carry the same obligation.
[[nodiscard, gnu::const]] constexpr int8_t h2_tag(uint64_t hash) noexcept { return static_cast<int8_t>(hash >> 57); }

static_assert(h2_tag(0x0000000000000000ULL) == 0, "h2_tag(0) must be 0");
static_assert(h2_tag(0xFFFFFFFFFFFFFFFFULL) == 127, "h2_tag(UINT64_MAX) must be 127 (top 7 bits all set, "
                                                    "bit 7 of int8_t clear)");
static_assert(h2_tag(0x8000000000000000ULL) == 64, "h2_tag with only bit 63 set must map to 0x40");

// Each set bit is a slot offset in [0, kGroupWidth).
struct BitMask {
    using Mask = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<kGroupMaskCeiling>, uint64_t>;

    static_assert(sizeof(Mask) == sizeof(uint64_t));
    static_assert(std::is_trivially_copyable_v<Mask>);
    static_assert(std::is_standard_layout_v<Mask>);

    constexpr BitMask() noexcept = default;
    constexpr explicit BitMask(uint64_t raw_mask) noexcept : mask_(raw_mask) {}
    constexpr explicit BitMask(Mask mask) noexcept : mask_(mask) {}

    [[nodiscard]] constexpr uint64_t raw() const noexcept { return mask_.value(); }

    [[nodiscard]] CRUCIBLE_INLINE explicit operator bool() const { return raw() != 0; }

    // std::countr_zero returns 64 for a zero mask. That is outside every
    // group's slot range, so a probe loop would read it as a real slot index.
    [[nodiscard]] CRUCIBLE_INLINE uint32_t lowest() const {
        CRUCIBLE_PRE(raw() != uint64_t{0});
        return static_cast<uint32_t>(std::countr_zero(raw()));
    }

    CRUCIBLE_INLINE void clear_lowest() {
        const uint64_t m = raw();
        mask_ = Mask{m & (m - uint64_t{1})};
    }

private:
    Mask mask_{uint64_t{0}};
};

struct CtrlGroup {
#if defined(__AVX512BW__)
    __m512i ctrl;

    [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
        // The intrinsic takes a pointer to the vector type. That type carries
        // the may_alias attribute, so the byte read is well defined.
        return {_mm512_loadu_si512(static_cast<const __m512i*>(static_cast<const void*>(pos)))};
    }

    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        return BitMask{static_cast<uint64_t>(_mm512_cmpeq_epi8_mask(ctrl, _mm512_set1_epi8(h2)))};
    }

    // kEmpty is the only control byte with bit 7 set, so extracting the sign
    // bit of each byte is equivalent to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        return BitMask{static_cast<uint64_t>(_mm512_movepi8_mask(ctrl))};
    }

#elif defined(__AVX2__)
    __m256i ctrl;

    [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
        // The intrinsic takes a pointer to the vector type. That type carries
        // the may_alias attribute, so the byte read is well defined.
        return {_mm256_loadu_si256(static_cast<const __m256i*>(static_cast<const void*>(pos)))};
    }

    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        auto cmp = _mm256_cmpeq_epi8(ctrl, _mm256_set1_epi8(h2));
        // The movemask result is a signed int. Widening through uint32_t
        // stops a set top bit from sign-extending across the upper 32 bits.
        return BitMask{static_cast<uint64_t>(static_cast<uint32_t>(_mm256_movemask_epi8(cmp)))};
    }

    // kEmpty is the only control byte with bit 7 set, so extracting the sign
    // bit of each byte is equivalent to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        return BitMask{static_cast<uint64_t>(static_cast<uint32_t>(_mm256_movemask_epi8(ctrl)))};
    }

#elif defined(__SSE2__)
    __m128i ctrl;

    [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
        // The intrinsic takes a pointer to the vector type. That type carries
        // the may_alias attribute, so the byte read is well defined.
        return {_mm_loadu_si128(static_cast<const __m128i*>(static_cast<const void*>(pos)))};
    }

    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        auto cmp = _mm_cmpeq_epi8(ctrl, _mm_set1_epi8(h2));
        // The movemask result is a signed int. Widening through uint32_t
        // stops a set top bit from sign-extending across the upper 32 bits.
        return BitMask{static_cast<uint64_t>(static_cast<uint32_t>(_mm_movemask_epi8(cmp)))};
    }

    // kEmpty is the only control byte with bit 7 set, so extracting the sign
    // bit of each byte is equivalent to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        return BitMask{static_cast<uint64_t>(static_cast<uint32_t>(_mm_movemask_epi8(ctrl)))};
    }

#elif defined(__aarch64__)
    int8x16_t ctrl;

    [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) { return {vld1q_s8(pos)}; }

    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        uint8x16_t cmp = vceqq_s8(ctrl, vdupq_n_s8(h2));
        return BitMask{neon_movemask(cmp)};
    }

    // kEmpty is the only negative control byte, so a sign test is equivalent
    // to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        uint8x16_t neg = vcltzq_s8(ctrl);
        return BitMask{neon_movemask(neg)};
    }

    // NEON has no movemask. Each input lane is 0xFF or 0x00, so masking with
    // a one-hot weight per lane and summing collapses each half to one byte
    // of the result.
    [[nodiscard]] CRUCIBLE_INLINE static uint64_t neon_movemask(uint8x16_t cmp) {
        static constexpr uint8_t kBits[16] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
                                              0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
        uint8x16_t bits = vld1q_u8(kBits);
        uint8x16_t masked = vandq_u8(cmp, bits);
        uint64x2_t sum = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(masked)));
        // Lane 0 holds bytes 0 through 7, lane 1 holds bytes 8 through 15.
        return vgetq_lane_u64(sum, 0) | (vgetq_lane_u64(sum, 1) << 8);
    }

#else
    int8_t bytes[16] = {};

    [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
        CtrlGroup g;
        std::memcpy(g.bytes, pos, 16);
        return g;
    }

    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const { return BitMask{swar_match(h2)}; }

    // kEmpty is the only control byte with bit 7 set, so gathering the high
    // bit of each byte is equivalent to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        uint64_t lo = 0, hi = 0;
        std::memcpy(&lo, bytes, 8);
        std::memcpy(&hi, bytes + 8, 8);
        uint64_t mask = extract_highbits(lo) | (extract_highbits(hi) << 8);
        return BitMask{mask};
    }

private:
    // Gathers bit 7 of each of the 8 bytes into bits 0 through 7 of the
    // result. The multiplier is chosen so that each surviving high bit lands
    // on a distinct position of the top byte and no two of them carry into
    // each other.
    [[nodiscard]] CRUCIBLE_INLINE static uint64_t extract_highbits(uint64_t v) {
        constexpr uint64_t hi_mask = 0x8080808080808080ULL;
        uint64_t bits = v & hi_mask;
        bits *= 0x0002040810204080ULL;
        return bits >> 56;
    }

    [[nodiscard]] CRUCIBLE_INLINE uint64_t swar_match(int8_t h2) const {
        uint64_t lo = 0, hi = 0;
        std::memcpy(&lo, bytes, 8);
        std::memcpy(&hi, bytes + 8, 8);

        uint64_t needle = static_cast<uint64_t>(static_cast<uint8_t>(h2)) * 0x0101010101010101ULL;

        // The xor turns every matching byte into zero.
        uint64_t xor_lo = lo ^ needle;
        uint64_t xor_hi = hi ^ needle;

        // For any byte b, b is zero exactly when ((b - 0x01) & ~b & 0x80) is
        // nonzero. Applied byte-wise, this leaves bit 7 set in each match.
        constexpr uint64_t lo_magic = 0x0101010101010101ULL;
        constexpr uint64_t hi_magic = 0x8080808080808080ULL;

        uint64_t zero_lo = (xor_lo - lo_magic) & ~xor_lo & hi_magic;
        uint64_t zero_hi = (xor_hi - lo_magic) & ~xor_hi & hi_magic;

        uint64_t mask = extract_highbits(zero_lo) | (extract_highbits(zero_hi) << 8);
        return mask;
    }
#endif
};

static_assert(sizeof(CtrlGroup) == group_width(), "CtrlGroup size must match SIMD group width");

}  // namespace detail
}  // namespace crucible
