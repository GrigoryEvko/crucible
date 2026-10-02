#pragma once

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Refined.h>
#include <fixy/atoms/Hw.h>
#include <fixy/atoms/Simd.h>
#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

// The x86 arms call GCC builtins, so they include no intrinsics header.
// Only the NEON arm includes one.
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace crucible {
namespace detail {

inline constexpr int8_t kEmpty = static_cast<int8_t>(0x80);

using GroupWidth = ::fixy::PowerOfTwo<std::size_t>;

#if defined(__AVX512BW__)
static constexpr GroupWidth kGroupWidth = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{64});
#elif defined(__AVX2__)
static constexpr GroupWidth kGroupWidth = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{32});
#else
static constexpr GroupWidth kGroupWidth = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{16});
#endif

[[nodiscard]] consteval std::size_t group_width() noexcept { return kGroupWidth.value(); }

// The ISA the probe was emitted for and the instruction class it issues,
// restated as fixy atoms of the arm the preprocessor selects above.
namespace swiss_hw {

namespace fas = ::fixy::atom::simd;
namespace fah = ::fixy::atom::hw;

#if defined(__AVX512BW__)
using ActiveSimdIsa = fas::avx512bw;
#elif defined(__AVX2__)
using ActiveSimdIsa = fas::avx2;
#elif defined(__SSE2__)
using ActiveSimdIsa = fas::sse2;
#elif defined(__aarch64__)
using ActiveSimdIsa = fas::neon;
#else
using ActiveSimdIsa = fas::scalar;
#endif

// The four vector arms issue vector instructions.  The portable arm runs
// SWAR over general-purpose registers, which is the scalar class.
using InstructionTier = std::conditional_t<fas::is_trunk_pinned(ActiveSimdIsa::isa), fah::vectorizable, fah::scalar>;

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
// boundary static_asserts in the check file of this header carry the same
// obligation.
[[nodiscard, gnu::const]] constexpr int8_t h2_tag(uint64_t hash) noexcept { return static_cast<int8_t>(hash >> 57); }

// Each set bit is a slot offset in [0, kGroupWidth).
struct BitMask {
    using Mask = ::fixy::Refined<::fixy::bounded_above<kGroupMaskCeiling>, uint64_t>;

    static_assert(sizeof(Mask) == sizeof(uint64_t));
    static_assert(std::is_trivially_copy_constructible_v<Mask> && std::is_trivially_move_constructible_v<Mask>
                  && std::is_trivially_destructible_v<Mask>);
    static_assert(std::is_standard_layout_v<Mask>);

    constexpr BitMask() noexcept = default;
    constexpr explicit BitMask(uint64_t raw_mask) noexcept : mask_(checked_mask_(raw_mask)) {}
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
        mask_ = checked_mask_(m & (m - uint64_t{1}));
    }

private:
    // Every raw word reaches the mask through the checked mint, so the
    // ceiling is tested at each construction, as a constant too.
    [[nodiscard]] static constexpr Mask checked_mask_(uint64_t raw_mask) noexcept {
        return ::fixy::mint_refined<::fixy::bounded_above<kGroupMaskCeiling>>(raw_mask);
    }

    Mask mask_ = checked_mask_(uint64_t{0});
};

struct CtrlGroup {
#if defined(__AVX512BW__) || defined(__AVX2__) || defined(__SSE2__)
    // One register of the active instruction set.  The register holds 64-bit
    // words, so an AVX-512 load of it is vmovdqu64.  The byte builtins take
    // lanes of char, and lanes of signed char do not convert to them, so each
    // operation reads the register as char lanes.
    using Words = ::foundation::simd::vec<long long, static_cast<int>(group_width() / 8U)>::raw_type;
    using Bytes = ::foundation::simd::vec<char, static_cast<int>(group_width())>;
    using Lanes = Bytes::raw_type;

    // The type of an unaligned load of one register.  The may_alias
    // attribute lets the load read the int8_t control bytes, and the
    // alignment of one byte makes no claim about the address.
    using UnalignedWords [[gnu::vector_size(group_width()), gnu::may_alias, gnu::aligned(1)]] = long long;

    Words ctrl;

    [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
        return {*static_cast<const UnalignedWords*>(static_cast<const void*>(pos))};
    }
#endif

#if defined(__AVX512BW__)
    // The write mask of all ones compares each of the 64 lanes.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        const Lanes needle = Bytes{static_cast<char>(h2)}.v_;
        return BitMask{
            static_cast<uint64_t>(__builtin_ia32_pcmpeqb512_mask(std::bit_cast<Lanes>(ctrl), needle, ~0ULL))};
    }

    // kEmpty is the only control byte with bit 7 set, so extracting the sign
    // bit of each byte is equivalent to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        return BitMask{static_cast<uint64_t>(__builtin_ia32_cvtb2mask512(std::bit_cast<Lanes>(ctrl)))};
    }

#elif defined(__AVX2__)
    // The comparison gives lanes of signed char.  The movemask builtin takes
    // lanes of char, and the bit cast keeps each bit.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        const Lanes needle = Bytes{static_cast<char>(h2)}.v_;
        const auto cmp = std::bit_cast<Lanes>(std::bit_cast<Lanes>(ctrl) == needle);
        // The movemask result is a signed int. Widening through uint32_t
        // stops a set top bit from sign-extending across the upper 32 bits.
        return BitMask{static_cast<uint64_t>(static_cast<uint32_t>(__builtin_ia32_pmovmskb256(cmp)))};
    }

    // kEmpty is the only control byte with bit 7 set, so extracting the sign
    // bit of each byte is equivalent to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        return BitMask{
            static_cast<uint64_t>(static_cast<uint32_t>(__builtin_ia32_pmovmskb256(std::bit_cast<Lanes>(ctrl))))};
    }

#elif defined(__SSE2__)
    // The comparison gives lanes of signed char.  The movemask builtin takes
    // lanes of char, and the bit cast keeps each bit.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        const Lanes needle = Bytes{static_cast<char>(h2)}.v_;
        const auto cmp = std::bit_cast<Lanes>(std::bit_cast<Lanes>(ctrl) == needle);
        // The movemask result is a signed int. Widening through uint32_t
        // stops a set top bit from sign-extending across the upper 32 bits.
        return BitMask{static_cast<uint64_t>(static_cast<uint32_t>(__builtin_ia32_pmovmskb128(cmp)))};
    }

    // kEmpty is the only control byte with bit 7 set, so extracting the sign
    // bit of each byte is equivalent to comparing every byte against kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        return BitMask{
            static_cast<uint64_t>(static_cast<uint32_t>(__builtin_ia32_pmovmskb128(std::bit_cast<Lanes>(ctrl))))};
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

}  // namespace detail
}  // namespace crucible
