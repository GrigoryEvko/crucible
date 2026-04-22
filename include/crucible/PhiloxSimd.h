#pragma once

// ═══════════════════════════════════════════════════════════════════
// PhiloxSimd.h — 8-way batched Philox4x32-10 (SIMD-9)
//
// Pure-std::simd batched counterpart to crucible::Philox::generate.
// Processes 8 independent (counter, key) pairs in parallel per call,
// producing 8 × (4 × uint32) = 32 random words per invocation.
//
// ─── SoA layout ─────────────────────────────────────────────────────
//
// Inputs are 4 + 2 vectors of u32x8.  Lane `i` of each vector is the
// `i`-th independent Philox call's input word:
//
//   ctr_v[w].lane[i] == scalar counter[i][w]    (w = 0..3)
//   key_v[w].lane[i] == scalar key[i][w]        (w = 0..1)
//
// Output PhiloxBatch8::r{0..3}:
//
//   result.r[w].lane[i] == scalar Philox::generate(counter[i], key[i])[w]
//
// This layout is the natural match for streaming RNG over a tensor:
// pre-load 8 contiguous element offsets into ctr0, broadcast the high
// 32 bits of the offset to ctr1, broadcast the key to key0/key1, and
// receive 4 × u32x8 of random data — no scalar fallback per element.
//
// ─── Per-round operations ───────────────────────────────────────────
//
// Each of the 10 rounds is identical to Philox::generate but on
// vectors:
//
//   mullo(a, b):   plain `u32x8 lo = a * b;`  (basic_vec * is the
//                  truncating low-32 multiply for u32 — exactly mullo)
//
//   mulhi(a, b):   cast-multiply-shift idiom:
//                    u64x8 prod = u64x8(a) * u64x8(b);
//                    u32x8 hi   = static_cast<u32x8>(prod >> 32);
//                  basic_vec's converting ctor zero-extends u32→u64
//                  per lane.  GCC emits `vpmuludq` on AVX2 (x4 lanes
//                  per ymm) and one `vpmullq` on AVX-512.
//
//   permute:       lane shuffle is data-flow only — assignment
//                  between the 4 ctr vectors, exactly mirroring the
//                  scalar `ctr = { hi1^ctr[1]^key[0], lo1, hi0^ctr[3]^key[1], lo0 }`.
//
//   weyl bump:     plain `key0 = key0 + W0;` etc. — broadcast add.
//
// 10 rounds, fully unrolled by the compiler.  No cross-lane operations;
// every lane is processed independently and identically — so per-lane
// output is bit-identical to scalar Philox::generate (verified by
// fuzzer prop_philox_simd_equivalence).
//
// ─── DetSafe contract (CLAUDE.md §II.8) ─────────────────────────────
//
// philox_batch8 MUST produce per-lane bit-identical output to scalar
// Philox::generate on every supported ISA.  Enforced by:
//
//   * Per-lane operations only.  Multiplication, XOR, addition all
//     act lane-wise; std::simd guarantees lane[i] of (a * b) is
//     a[i] * b[i], regardless of ABI or microarch.
//   * Truncating u32 multiplication is well-defined per IEEE/C++
//     unsigned wraparound; matches scalar `uint32_t * uint32_t`.
//   * u64 widening + shift-right-32 reproduces the high 32 bits of
//     the 64-bit product, which is exactly what scalar mulhi_ does.
//   * NO reductions.  Each lane stays in its lane through all 10
//     rounds and is returned in place.
//
// ─── Performance target (SIMD-9) ────────────────────────────────────
//
// Scalar baseline (Zen4 @ 4.7 GHz, single Philox::generate):
//   ~7 ns/call → 8 calls = ~56 ns
//
// SIMD target (one philox_batch8 call):
//   ≤25 ns for 8 calls = ≥3× speedup.
//   On AVX-512: ~15 ns expected (one zmm per ctr/key vector;
//   vpmullq native; full pipeline utilization).
//   On AVX2:   ~25 ns expected (vpmuludq twice per mulhi).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Philox.h>
#include <crucible/Platform.h>
#include <crucible/safety/Simd.h>

#include <cstdint>
#include <simd>

namespace crucible::detail {

// ── PhiloxBatch8 ──────────────────────────────────────────────────
//
// 4 × u32x8 = 128 bytes (2 cache lines, 4 ymm registers, 2 zmm
// registers).  Fits in the SysV ABI's vector-register return path
// — no spill on x86-64.

struct PhiloxBatch8 {
  simd::u32x8 r0;
  simd::u32x8 r1;
  simd::u32x8 r2;
  simd::u32x8 r3;
};

// ── philox_batch8 ─────────────────────────────────────────────────
//
// 8 parallel Philox4x32-10 calls.  Per-lane bit-equivalent to the
// scalar oracle:
//
//   for lane i in [0, 8):
//     auto scalar = Philox::generate(
//         { ctr0[i], ctr1[i], ctr2[i], ctr3[i] },
//         { key0[i], key1[i] });
//     assert(scalar[0] == result.r0[i]);
//     assert(scalar[1] == result.r1[i]);
//     assert(scalar[2] == result.r2[i]);
//     assert(scalar[3] == result.r3[i]);
//
// gnu::const: pure function of by-value SIMD args, no side effects,
// no memory access.  Safe to CSE.

[[nodiscard, gnu::const]] CRUCIBLE_INLINE
PhiloxBatch8 philox_batch8(
    simd::u32x8 ctr0, simd::u32x8 ctr1,
    simd::u32x8 ctr2, simd::u32x8 ctr3,
    simd::u32x8 key0, simd::u32x8 key1) noexcept {
  using simd::u32x8;
  using simd::u64x8;

  // Broadcast Philox constants across all 8 lanes.  Compile-time
  // constants — the compiler keeps these in vector registers across
  // the unrolled rounds (no per-round reload).
  const u32x8 m0(Philox::M0);
  const u32x8 m1(Philox::M1);
  const u32x8 w0(Philox::W0);
  const u32x8 w1(Philox::W1);

  // 10 unrolled rounds.  The trip count is fixed at compile time,
  // and -O2 unrolls deterministically; the round body is small
  // enough that the unrolled body still fits comfortably in L1i.
  for (int round = 0; round < 10; ++round) {
    // Truncating multiplies (low 32 bits of u32 × u32).
    const u32x8 lo0 = ctr0 * m0;
    const u32x8 lo1 = ctr2 * m1;

    // High-32 multiplies via the cast-multiply-shift idiom.  The
    // basic_vec converting ctor u64x8(u32x8) zero-extends per lane;
    // multiplying two u64x8 then shifting right by 32 yields the
    // high 32 bits of the unsigned 32×32→64 product, identical to
    // scalar Philox::mulhi_.
    const u64x8 prod0 = u64x8(ctr0) * u64x8(m0);
    const u64x8 prod1 = u64x8(ctr2) * u64x8(m1);
    const u32x8 hi0 = static_cast<u32x8>(prod0 >> 32);
    const u32x8 hi1 = static_cast<u32x8>(prod1 >> 32);

    // Lane permutation matching scalar:
    //   ctr = { hi1 ^ ctr[1] ^ key[0],
    //           lo1,
    //           hi0 ^ ctr[3] ^ key[1],
    //           lo0 }
    // Read all of ctr1/ctr3 BEFORE writing ctr0/ctr2 — the four new
    // values are computed into named locals so the assignment order
    // can't accidentally clobber inputs.
    const u32x8 nctr0 = hi1 ^ ctr1 ^ key0;
    const u32x8 nctr1 = lo1;
    const u32x8 nctr2 = hi0 ^ ctr3 ^ key1;
    const u32x8 nctr3 = lo0;
    ctr0 = nctr0;
    ctr1 = nctr1;
    ctr2 = nctr2;
    ctr3 = nctr3;

    // Key schedule: Weyl sequence bump.  Broadcast adds, one per
    // key half.
    key0 = key0 + w0;
    key1 = key1 + w1;
  }

  return {ctr0, ctr1, ctr2, ctr3};
}

}  // namespace crucible::detail
