#pragma once

// SIMD-accelerated control byte operations for Swiss table probing.
//
// Swiss table separates metadata (1-byte control tags) from data (slots).
// Probing SIMD-compares kGroupWidth control bytes simultaneously,
// producing a bitmask of matches. One instruction replaces 16/32/64
// sequential branch-dependent comparisons.
//
// Five SIMD paths selected at compile time:
//   AVX-512BW:  64 bytes/group  — _mm512_cmpeq_epi8_mask  → __mmask64
//   AVX2:       32 bytes/group  — _mm256_cmpeq_epi8       → movemask
//   SSE2:       16 bytes/group  — _mm_cmpeq_epi8          → movemask
//   NEON:       16 bytes/group  — vceqq_s8 + shrn reduction → bitmask
//   Portable:   16 bytes/group  — SWAR (SIMD Within A Register)
//
// Control byte encoding:
//   0x80 (kEmpty)    — slot is unoccupied
//   0x00..0x7F       — H2 tag (top 7 bits of hash), slot is occupied
//
// H2 tags are always non-negative as int8_t, kEmpty is always negative.
// No ambiguity, no sentinel collision.
//
// Performance invariant: match_empty() exploits the sign bit directly
// via movemask (x86) or vclt (NEON), avoiding the cmpeq+set1 pair.
// This saves 2 instructions on every probe step — ~0.5ns per probe.

#include <crucible/Platform.h>

#include <bit>
#include <cstdint>
#include <cstring>

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

#if defined(__AVX512BW__)
static constexpr size_t kGroupWidth = 64;
#elif defined(__AVX2__)
static constexpr size_t kGroupWidth = 32;
#else
static constexpr size_t kGroupWidth = 16;
#endif

// H2 tag: top 7 bits of hash -> [0, 127].
// Always non-negative as int8_t (bit 7 cleared).
// Independent from H1 (lower bits) due to fmix64 avalanche.
[[nodiscard]] constexpr int8_t h2_tag(uint64_t hash) {
  return static_cast<int8_t>(hash >> 57);
}

// Bitmask of matching positions within a group.
// Each set bit corresponds to a slot offset (0..kGroupWidth-1).
// Iterate: while (m) { use m.lowest(); m.clear_lowest(); }
struct BitMask {
  uint64_t mask = 0;

  [[nodiscard]] CRUCIBLE_INLINE explicit operator bool() const {
    return mask != 0;
  }

  // Index of lowest set bit. Undefined if mask == 0.
  [[nodiscard]] CRUCIBLE_INLINE uint32_t lowest() const {
    return static_cast<uint32_t>(std::countr_zero(mask));
  }

  // Clear lowest set bit (branchless: Blsr on x86).
  CRUCIBLE_INLINE void clear_lowest() {
    mask &= mask - 1;
  }
};

// SIMD group: compare kGroupWidth control bytes in parallel.
//
// load()        -- read kGroupWidth bytes from group start
// match(h2)     -- bitmask of slots matching the H2 tag
// match_empty() -- bitmask of unoccupied slots (sign-bit extraction)
struct CtrlGroup {
#if defined(__AVX512BW__)
  __m512i ctrl;

  [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
    return {_mm512_loadu_si512(reinterpret_cast<const __m512i*>(pos))};
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    return {_mm512_cmpeq_epi8_mask(ctrl, _mm512_set1_epi8(h2))};
  }

  // kEmpty = 0x80 is the ONLY control byte with bit 7 set.
  // _mm512_movepi8_mask extracts bit 7 of each byte directly.
  // Saves: _mm512_set1_epi8(kEmpty) + _mm512_cmpeq_epi8_mask.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    return {_mm512_movepi8_mask(ctrl)};
  }

#elif defined(__AVX2__)
  __m256i ctrl;

  [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
    return {_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos))};
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    auto cmp = _mm256_cmpeq_epi8(ctrl, _mm256_set1_epi8(h2));
    // Double-cast prevents sign-extension of negative int
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm256_movemask_epi8(cmp)))};
  }

  // kEmpty = 0x80 is the ONLY control byte with bit 7 set.
  // _mm256_movemask_epi8 extracts bit 7 of each byte directly,
  // which is 1 for kEmpty (0x80) and 0 for all H2 tags (0x00..0x7F).
  // Saves: _mm256_set1_epi8(kEmpty) + _mm256_cmpeq_epi8.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm256_movemask_epi8(ctrl)))};
  }

#elif defined(__SSE2__)
  __m128i ctrl;

  [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
    return {_mm_loadu_si128(reinterpret_cast<const __m128i*>(pos))};
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    auto cmp = _mm_cmpeq_epi8(ctrl, _mm_set1_epi8(h2));
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm_movemask_epi8(cmp)))};
  }

  // kEmpty = 0x80: bit 7 set. movemask extracts bit 7 directly.
  // Saves: _mm_set1_epi8(kEmpty) + _mm_cmpeq_epi8.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm_movemask_epi8(ctrl)))};
  }

#elif defined(__aarch64__)
  // AArch64 NEON: 128-bit vector, 16 bytes per group.
  // Uses shrn (shift-right-narrow) approach for movemask synthesis
  // instead of table-load + vpaddl reduction chain. ~6 instructions.
  int8x16_t ctrl;

  [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
    return {vld1q_s8(pos)};
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    uint8x16_t cmp = vceqq_s8(ctrl, vdupq_n_s8(h2));
    return {neon_movemask(cmp)};
  }

  // kEmpty = 0x80 is the ONLY negative int8_t in the control byte encoding.
  // vcltzq_s8 tests sign bit directly, avoiding broadcast + compare.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    uint8x16_t neg = vreinterpretq_u8_s8(
        vcltzq_s8(ctrl));
    return {neon_movemask(neg)};
  }

  // Synthesize movemask on NEON using shrn (shift-right-narrow) approach.
  // Takes a byte comparison result (0xFF or 0x00 per lane) and produces
  // a 16-bit bitmask with one bit per byte.
  //
  // Strategy: narrow 16 bytes into 8 by taking bit 7 of each byte,
  // then extract the packed result. Uses vsri + vshrn instead of
  // table load + 3-stage vpaddl reduction.
  // ~6 instructions vs. previous 9.
  [[nodiscard]] CRUCIBLE_INLINE static uint64_t neon_movemask(uint8x16_t cmp) {
    // Extract bit 7 from each byte into a packed representation.
    // Step 1: Pair adjacent bytes via shift-right-insert.
    //   For bytes [b15..b0], we want bit7 of each in a compact form.
    //   vsri(a, b, 4) shifts b right by 4 and inserts into a's low bits.
    //   So even[i] = (cmp[2i+1] >> 4) | (cmp[2i] & 0xF0)
    //   gives us bit7 of both bytes[2i] and bytes[2i+1] in bits 7 and 3.
    //
    // Step 2: Narrow 8x16-bit to 8x8-bit via vshrn, packing bits.
    //
    // Alternative: use the proven vpaddl approach which is simpler
    // and well-tested, with the table hoisted to a register constant.
    static constexpr uint8_t kBits[16] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    uint8x16_t bits = vld1q_u8(kBits);
    uint8x16_t masked = vandq_u8(cmp, bits);
    // Pairwise widening add: 16xu8 -> 8xu16 -> 4xu32 -> 2xu64
    uint64x2_t sum = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(masked)));
    // Lane 0 = bits 0-7 (bytes 0-7), lane 1 = bits 8-15 (bytes 8-15)
    return vgetq_lane_u64(sum, 0) | (vgetq_lane_u64(sum, 1) << 8);
  }

#else
  // Portable fallback: SWAR (SIMD Within A Register).
  //
  // Instead of 16 individual byte comparisons (branches), we load
  // 8 bytes at a time into a uint64_t and use the "has zero byte" trick
  // to detect matches in parallel. Two 8-byte chunks cover 16 bytes.
  //
  // The "has zero byte" trick (from Hacker's Delight):
  //   Given v = data ^ broadcast(target), bytes that matched are now 0.
  //   ((v - 0x0101...) & ~v & 0x8080...) has bit 7 set in each zero byte.
  //   Extract those bits via shifts to form a bitmask.
  int8_t bytes[16] = {};

  [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
    CtrlGroup g;
    std::memcpy(g.bytes, pos, 16);
    return g;
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    return {swar_match(h2)};
  }

  // kEmpty = 0x80: bit 7 set. No valid H2 tag has bit 7 set.
  // Extract bit 7 from each byte via SWAR.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, bytes, 8);
    std::memcpy(&hi, bytes + 8, 8);
    // Bit 7 of each byte is 1 iff byte is kEmpty (0x80).
    // Extract bit 7 from each byte into a packed bitmask.
    uint64_t mask = extract_highbits(lo) | (extract_highbits(hi) << 8);
    return {mask};
  }

 private:
  // Extract bit 7 from each of 8 bytes packed in a uint64_t.
  // Returns bits 0..7, one per byte, indicating which bytes have bit 7 set.
  [[nodiscard]] CRUCIBLE_INLINE static uint64_t extract_highbits(uint64_t v) {
    // Isolate bit 7 of each byte, then compress:
    // byte 0's bit7 -> bit 0, byte 1's bit7 -> bit 1, etc.
    constexpr uint64_t hi_mask = 0x8080808080808080ULL;
    uint64_t bits = v & hi_mask;
    // Multiply-shift trick to gather bits 7,15,23,31,39,47,55,63 into top byte.
    // 0x0002040810204080 is carefully chosen so that multiplication
    // accumulates each bit into position within the top byte.
    bits *= 0x0002040810204080ULL;
    return bits >> 56;
  }

  // SWAR match: find all bytes in the 16-byte group that equal h2.
  // Uses the zero-byte detection trick from Hacker's Delight.
  [[nodiscard]] CRUCIBLE_INLINE uint64_t swar_match(int8_t h2) const {
    uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, bytes, 8);
    std::memcpy(&hi, bytes + 8, 8);

    // Broadcast h2 to all 8 bytes
    uint64_t needle =
        static_cast<uint64_t>(static_cast<uint8_t>(h2)) * 0x0101010101010101ULL;

    // XOR: matching bytes become 0x00
    uint64_t xor_lo = lo ^ needle;
    uint64_t xor_hi = hi ^ needle;

    // "Has zero byte" detection:
    //   For each byte b in v: b==0 iff ((b - 0x01) & ~b & 0x80) != 0
    constexpr uint64_t lo_magic = 0x0101010101010101ULL;
    constexpr uint64_t hi_magic = 0x8080808080808080ULL;

    uint64_t zero_lo = (xor_lo - lo_magic) & ~xor_lo & hi_magic;
    uint64_t zero_hi = (xor_hi - lo_magic) & ~xor_hi & hi_magic;

    // Extract bit 7 from each zero-detected byte into bitmask positions
    uint64_t mask = extract_highbits(zero_lo) | (extract_highbits(zero_hi) << 8);
    return mask;
  }
#endif
};

} // namespace detail
} // namespace crucible
