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
//   NEON:       16 bytes/group  — vceqq_s8 + vpaddl reduction → bitmask
//   Portable:   16 bytes/group  — byte loop (fallback)
//
// Control byte encoding:
//   0x80 (kEmpty)    — slot is unoccupied
//   0x00..0x7F       — H2 tag (top 7 bits of hash), slot is occupied
//
// H2 tags are always non-negative as int8_t, kEmpty is always negative.
// No ambiguity, no sentinel collision.

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

// H2 tag: top 7 bits of hash → [0, 127].
// Always non-negative as int8_t (bit 7 cleared).
// Independent from H1 (lower bits) due to fmix64 avalanche.
inline int8_t h2_tag(uint64_t hash) {
  return static_cast<int8_t>(hash >> 57);
}

// Bitmask of matching positions within a group.
// Each set bit corresponds to a slot offset (0..kGroupWidth-1).
// Iterate: while (m) { use m.lowest(); m.clear_lowest(); }
struct BitMask {
  uint64_t mask;

  explicit operator bool() const {
    return mask != 0;
  }

  // Index of lowest set bit. Undefined if mask == 0.
  uint32_t lowest() const {
    return static_cast<uint32_t>(std::countr_zero(mask));
  }

  // Clear lowest set bit (branchless).
  void clear_lowest() {
    mask &= mask - 1;
  }
};

// SIMD group: compare kGroupWidth control bytes in parallel.
//
// load()        — read kGroupWidth bytes from group start
// match(h2)     — bitmask of slots matching the H2 tag
// match_empty() — bitmask of unoccupied slots
struct CtrlGroup {
#if defined(__AVX512BW__)
  __m512i ctrl;

  static CtrlGroup load(const int8_t* pos) {
    return {_mm512_loadu_si512(reinterpret_cast<const __m512i*>(pos))};
  }

  BitMask match(int8_t h2) const {
    return {_mm512_cmpeq_epi8_mask(ctrl, _mm512_set1_epi8(h2))};
  }

  BitMask match_empty() const {
    return {_mm512_cmpeq_epi8_mask(ctrl, _mm512_set1_epi8(kEmpty))};
  }

#elif defined(__AVX2__)
  __m256i ctrl;

  static CtrlGroup load(const int8_t* pos) {
    return {_mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos))};
  }

  BitMask match(int8_t h2) const {
    auto cmp = _mm256_cmpeq_epi8(ctrl, _mm256_set1_epi8(h2));
    // Double-cast prevents sign-extension of negative int
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm256_movemask_epi8(cmp)))};
  }

  BitMask match_empty() const {
    auto cmp = _mm256_cmpeq_epi8(ctrl, _mm256_set1_epi8(kEmpty));
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm256_movemask_epi8(cmp)))};
  }

#elif defined(__SSE2__)
  __m128i ctrl;

  static CtrlGroup load(const int8_t* pos) {
    return {_mm_loadu_si128(reinterpret_cast<const __m128i*>(pos))};
  }

  BitMask match(int8_t h2) const {
    auto cmp = _mm_cmpeq_epi8(ctrl, _mm_set1_epi8(h2));
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm_movemask_epi8(cmp)))};
  }

  BitMask match_empty() const {
    auto cmp = _mm_cmpeq_epi8(ctrl, _mm_set1_epi8(kEmpty));
    return {static_cast<uint64_t>(
        static_cast<uint32_t>(_mm_movemask_epi8(cmp)))};
  }

#elif defined(__aarch64__)
  // AArch64 NEON: 128-bit vector, 16 bytes per group.
  // NEON lacks movemask — we synthesize it via AND + pairwise-add reduction.
  int8x16_t ctrl;

  static CtrlGroup load(const int8_t* pos) {
    return {vld1q_s8(pos)};
  }

  BitMask match(int8_t h2) const {
    uint8x16_t cmp = vceqq_s8(ctrl, vdupq_n_s8(h2));
    return {neon_movemask(cmp)};
  }

  BitMask match_empty() const {
    uint8x16_t cmp = vceqq_s8(ctrl, vdupq_n_s8(kEmpty));
    return {neon_movemask(cmp)};
  }

  // Synthesize movemask on NEON:
  // 1. AND comparison result with power-of-2 table → isolate one bit per byte
  // 2. Three vpaddl reductions (16×u8 → 8×u16 → 4×u32 → 2×u64)
  // 3. Combine two 8-bit halves → 16-bit bitmask
  // ~9 instructions vs. x86's single pmovmskb.
  static uint64_t neon_movemask(uint8x16_t cmp) {
    static constexpr uint8_t kBits[16] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    uint8x16_t bits = vld1q_u8(kBits);
    uint8x16_t masked = vandq_u8(cmp, bits);
    // Pairwise widening add: 16×u8 → 8×u16 → 4×u32 → 2×u64
    uint64x2_t sum = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(masked)));
    // Lane 0 = bits 0-7 (bytes 0-7), lane 1 = bits 8-15 (bytes 8-15)
    return vgetq_lane_u64(sum, 0) | (vgetq_lane_u64(sum, 1) << 8);
  }

#else
  // Portable fallback: byte-by-byte comparison.
  int8_t bytes[16];

  static CtrlGroup load(const int8_t* pos) {
    CtrlGroup g;
    std::memcpy(g.bytes, pos, 16);
    return g;
  }

  BitMask match(int8_t h2) const {
    uint64_t m = 0;
    for (size_t i = 0; i < 16; ++i)
      m |= static_cast<uint64_t>(bytes[i] == h2) << i;
    return {m};
  }

  BitMask match_empty() const {
    return match(kEmpty);
  }
#endif
};

} // namespace detail
} // namespace crucible
