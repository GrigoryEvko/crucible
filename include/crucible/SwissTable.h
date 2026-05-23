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
#include <crucible/fixy/Wrap.h>              // FIXY-U-096i: PowerOfTwo + Refined + bounded_above
#include <crucible/fixy/Vendor.h>            // FIXY-V-262: vendor::intrinsic<V,I> + canonical aliases
#include <crucible/fixy/Simd.h>              // FIXY-V-262: simd::width<W> + width_* aliases
#include <crucible/safety/Decide.h>          // decide::* lives at top-level, not under safety::

// FIXY-U-096i production migration: PowerOfTwo / Refined / bounded_above
// reached through the fixy:: umbrella instead of safety::* directly.
// SwissTable.h is foundational (fan-in: ExprPool + 1 positive test + 4 neg-
// compile fixtures, all runtime-tier).  The fixy/Wrap.h umbrella's transitive
// Arena.h pull is redundant — Arena.h has no edge back to SwissTable.h —
// not cyclic.  GroupWidth + BitMask::Mask type identity preserved via the
// using-decl re-exports in fixy/Wrap.h, so existing tests + neg-compile
// fixtures that reference detail::GroupWidth / BitMask::Mask still compile
// unchanged.

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>                           // FIXY-V-262: std::to_underlying for the width-byte consistency assert

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

[[nodiscard]] consteval std::size_t group_width() noexcept {
  return kGroupWidth.value();
}

// CONTRACT-109: kGroupWidth is structurally a power of two — the SIMD
// path selection above hardcodes 16/32/64.  The static_assert pins the
// invariant at the definition site through the named predicate
// `decide::is_power_of_two_le<size_t>(kGroupWidth, 64)`:
//
//   * is_power_of_two_le verifies pow2 AND `≤ 64` — the inclusive upper
//     bound is the AVX-512 group width, which is the widest path the
//     Swiss-table dispatch supports.  A future add-of-AVX10 path that
//     bumps kGroupWidth to 128 would trip this assert and force an
//     audit of the BitMask types (currently uint64_t mask = 64 bits)
//     and the H2 tag distribution (currently 7-bit tag, plenty for
//     kGroupWidth=128).
//
//   * The cite is the grep-discoverable VC-discharge surface.  Future
//     hardening (e.g., RecipePool / ExprPool capacity invariants
//     wanting "kGroupWidth divides table_size") routes through the
//     same predicate name.
//
// CRUCIBLE_PRE not used here — kGroupWidth is a constant evaluated at
// translation-unit load time, so a plain static_assert binds the same
// VC obligation at compile time without runtime cost.
static_assert(::crucible::decide::is_power_of_two_le<std::size_t>(
                  group_width(), std::size_t{64}),
              "kGroupWidth must be a power of two ≤ 64 (AVX-512 width)");

// ── FIXY-V-262: hardware-axis grant declarations ───────────────────
//
// Each compile-time SIMD arm declares — at the TYPE level — which
// vendor::intrinsic<V, I> (FIXY-V-258) and simd::width<W> (FIXY-V-259)
// the active build uses for the control-byte probe.  This promotes the
// SwissTable's hardware dependency from an invisible `#ifdef` to a
// named, greppable, type-checked surface that the V-264
// check-fixy-hw-discipline.sh lint reads.  Zero runtime cost — empty
// grant tags + using-aliases, all consumed at compile time.
//
//   AVX-512BW → avx512bw_intrinsic + width_512   (64-byte group)
//   AVX2      → avx2_intrinsic     + width_256   (32-byte group)
//   SSE2      → sse2_intrinsic     + width_128   (16-byte group)
//   NEON      → neon_intrinsic     + width_128   (16-byte group)
//   Portable  → width_scalar, NO vendor intrinsic (16-byte SWAR over
//               general-purpose registers — no SIMD vector register;
//               the absence of a vendor dependency IS the point of the
//               portable fallback).
//
// `ActiveSimdWidth` is defined on every arm; `ActiveVendorIsa` only on
// the four real-SIMD arms.  The per-arm static_assert pins the declared
// register width (bits) to the actual group width (bytes × 8), so a
// future kGroupWidth edit that forgets to update the width grant reds
// at the definition site.
namespace swiss_hw {

namespace fv = ::crucible::fixy::vendor;
namespace fs = ::crucible::fixy::simd;

#if defined(__AVX512BW__)
using ActiveVendorIsa = fv::avx512bw_intrinsic;
using ActiveSimdWidth = fs::width_512;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits512),
              "FIXY-V-262: AVX-512BW group bytes × 8 must equal width_512 bits");
#elif defined(__AVX2__)
using ActiveVendorIsa = fv::avx2_intrinsic;
using ActiveSimdWidth = fs::width_256;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits256),
              "FIXY-V-262: AVX2 group bytes × 8 must equal width_256 bits");
#elif defined(__SSE2__)
using ActiveVendorIsa = fv::sse2_intrinsic;
using ActiveSimdWidth = fs::width_128;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits128),
              "FIXY-V-262: SSE2 group bytes × 8 must equal width_128 bits");
#elif defined(__aarch64__)
using ActiveVendorIsa = fv::neon_intrinsic;
using ActiveSimdWidth = fs::width_128;
static_assert(group_width() * 8u == std::to_underlying(fs::WidthBits::Bits128),
              "FIXY-V-262: NEON group bytes × 8 must equal width_128 bits");
#else
using ActiveSimdWidth = fs::width_scalar;  // portable SWAR — no vector register
#endif

// Arm-independent: the active SIMD-width grant is always well-formed and
// routes to the SimdIsa axis (FIXY-V-253).
static_assert(::crucible::fixy::grant::IsGrantTag<ActiveSimdWidth>,
              "FIXY-V-262: the active simd::width grant must be well-formed");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveSimdWidth>
                  == ::crucible::fixy::dim::DimensionAxis::SimdIsa,
              "FIXY-V-262: simd::width routes to the SimdIsa axis");

#if defined(__AVX512BW__) || defined(__AVX2__) || defined(__SSE2__) || defined(__aarch64__)
// The four real-SIMD arms additionally pin a vendor intrinsic; the
// portable SWAR fallback has no vendor dependency.
static_assert(::crucible::fixy::grant::IsGrantTag<ActiveVendorIsa>,
              "FIXY-V-262: the active vendor::intrinsic grant must be well-formed");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveVendorIsa>
                  == ::crucible::fixy::dim::DimensionAxis::HwInstruction,
              "FIXY-V-262: vendor::intrinsic routes to the HwInstruction axis");
#endif

}  // namespace swiss_hw

[[nodiscard]] consteval uint64_t group_mask_ceiling(std::size_t width) noexcept {
  return width == 64 ? std::numeric_limits<uint64_t>::max()
                     : ((uint64_t{1} << width) - uint64_t{1});
}

static constexpr uint64_t kGroupMaskCeiling = group_mask_ceiling(group_width());

// H2 tag: top 7 bits of hash -> [0, 127].
// Always non-negative as int8_t (bit 7 cleared).
// Independent from H1 (lower bits) due to fmix64 avalanche.
//
// The [0, 127] invariant is essential: kEmpty = 0x80 is the sentinel
// for empty slots in the control byte array; h2_tag returning a value
// with bit 7 set would alias an empty slot, corrupting the probe loop's
// termination condition.  The shift by 57 on uint64_t mathematically
// guarantees the top bit of the cast is clear, so the bit-7-clear
// property holds for ANY input — proven by the boundary static_asserts
// below (h2_tag(0)=0, h2_tag(UINT64_MAX)=127, h2_tag(0x8000…0)=64).
//
// We previously had `post (r: r >= 0)` here; removed because GCC 16.1.1
// post-PR-c++/124241 (May 4 2026 cache-fix) rejects always-true post
// clauses on constexpr functions whose body folds to a constant — the
// constexpr evaluator now over-invalidates and produces "contract
// condition is not constant".  The static_asserts below are stronger
// (boundary-input proofs) than the post() ever was.  See
// misc/08_05_2026_harness.md for the diagnosis.
[[nodiscard, gnu::const]] constexpr int8_t h2_tag(uint64_t hash) noexcept
{
  return static_cast<int8_t>(hash >> 57);
}

// Compile-time proof: for every uint64_t input, h2_tag produces a
// non-negative int8_t (bit 7 clear).  This expresses the sentinel-space
// reservation as an invariant rather than a runtime check.  If h2_tag's
// implementation drifts (e.g. `hash >> 56` leaking bit 7), either the
// static_assert or the post() contract trips.
static_assert(h2_tag(0x0000000000000000ULL) == 0,
              "h2_tag(0) must be 0");
static_assert(h2_tag(0xFFFFFFFFFFFFFFFFULL) == 127,
              "h2_tag(UINT64_MAX) must be 127 (top 7 bits all set, "
              "bit 7 of int8_t clear)");
static_assert(h2_tag(0x8000000000000000ULL) == 64,
              "h2_tag with only bit 63 set must map to 0x40");

// Bitmask of matching positions within a group.
// Each set bit corresponds to a slot offset (0..kGroupWidth-1).
// Iterate: while (m) { use m.lowest(); m.clear_lowest(); }
struct BitMask {
  using Mask = ::crucible::fixy::wrap::Refined<
      ::crucible::fixy::wrap::bounded_above<kGroupMaskCeiling>, uint64_t>;

  static_assert(sizeof(Mask) == sizeof(uint64_t));
  static_assert(std::is_trivially_copyable_v<Mask>);
  static_assert(std::is_standard_layout_v<Mask>);

  constexpr BitMask() noexcept = default;
  constexpr explicit BitMask(uint64_t raw_mask) noexcept : mask_(raw_mask) {}
  constexpr explicit BitMask(Mask mask) noexcept : mask_(mask) {}

  [[nodiscard]] constexpr uint64_t raw() const noexcept {
    return mask_.value();
  }

  [[nodiscard]] CRUCIBLE_INLINE explicit operator bool() const {
    return raw() != 0;
  }

  // Index of lowest set bit. Undefined if mask == 0.
  [[nodiscard]] CRUCIBLE_INLINE uint32_t lowest() const {
    return static_cast<uint32_t>(std::countr_zero(raw()));
  }

  // Clear lowest set bit (branchless: Blsr on x86).
  CRUCIBLE_INLINE void clear_lowest() {
    const uint64_t m = raw();
    mask_ = Mask{m & (m - uint64_t{1})};
  }

 private:
  Mask mask_{uint64_t{0}};
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
    // §III-clean pointer cast cascade: int8_t* → void* → __m512i*.
    // The unaligned-load intrinsic prototype demands pointer-to-vector-type,
    // and __m512i is GCC-attribute may_alias so the actual byte read is
    // well-defined regardless of declared pointee type.
    return {_mm512_loadu_si512(
        static_cast<const __m512i*>(static_cast<const void*>(pos)))};
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    return BitMask{
        static_cast<uint64_t>(_mm512_cmpeq_epi8_mask(ctrl, _mm512_set1_epi8(h2)))};
  }

  // kEmpty = 0x80 is the ONLY control byte with bit 7 set.
  // _mm512_movepi8_mask extracts bit 7 of each byte directly.
  // Saves: _mm512_set1_epi8(kEmpty) + _mm512_cmpeq_epi8_mask.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    return BitMask{static_cast<uint64_t>(_mm512_movepi8_mask(ctrl))};
  }

#elif defined(__AVX2__)
  __m256i ctrl;

  [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
    // §III-clean cast cascade: int8_t* → void* → __m256i*.  See AVX-512 arm.
    return {_mm256_loadu_si256(
        static_cast<const __m256i*>(static_cast<const void*>(pos)))};
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    auto cmp = _mm256_cmpeq_epi8(ctrl, _mm256_set1_epi8(h2));
    // Double-cast prevents sign-extension of negative int
    return BitMask{static_cast<uint64_t>(
        static_cast<uint32_t>(_mm256_movemask_epi8(cmp)))};
  }

  // kEmpty = 0x80 is the ONLY control byte with bit 7 set.
  // _mm256_movemask_epi8 extracts bit 7 of each byte directly,
  // which is 1 for kEmpty (0x80) and 0 for all H2 tags (0x00..0x7F).
  // Saves: _mm256_set1_epi8(kEmpty) + _mm256_cmpeq_epi8.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    return BitMask{static_cast<uint64_t>(
        static_cast<uint32_t>(_mm256_movemask_epi8(ctrl)))};
  }

#elif defined(__SSE2__)
  __m128i ctrl;

  [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
    // §III-clean cast cascade: int8_t* → void* → __m128i*.  See AVX-512 arm.
    return {_mm_loadu_si128(
        static_cast<const __m128i*>(static_cast<const void*>(pos)))};
  }

  [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
    auto cmp = _mm_cmpeq_epi8(ctrl, _mm_set1_epi8(h2));
    return BitMask{static_cast<uint64_t>(
        static_cast<uint32_t>(_mm_movemask_epi8(cmp)))};
  }

  // kEmpty = 0x80: bit 7 set. movemask extracts bit 7 directly.
  // Saves: _mm_set1_epi8(kEmpty) + _mm_cmpeq_epi8.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    return BitMask{static_cast<uint64_t>(
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
    return BitMask{neon_movemask(cmp)};
  }

  // kEmpty = 0x80 is the ONLY negative int8_t in the control byte encoding.
  // vcltzq_s8 tests sign bit directly, avoiding broadcast + compare.
  [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
    uint8x16_t neg = vreinterpretq_u8_s8(
        vcltzq_s8(ctrl));
    return BitMask{neon_movemask(neg)};
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
    return BitMask{swar_match(h2)};
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
    return BitMask{mask};
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

// Layout lock: CtrlGroup is a single SIMD register's worth of control
// bytes.  Vector-width mismatch between sizeof(CtrlGroup) and kGroupWidth
// would break the load/match bitmask width accounting — catch here.
// NEON aarch64 keeps kGroupWidth = 16 (SSE2 fallthrough in the kGroupWidth
// chain), matching int8x16_t's size.
static_assert(sizeof(CtrlGroup) == group_width(),
              "CtrlGroup size must match SIMD group width");

} // namespace detail
} // namespace crucible
