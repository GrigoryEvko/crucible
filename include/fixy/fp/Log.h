#pragma once

// log of a double and of a float, with the same bits on each target and each
// level of the instruction set.
//
//   fixy::fp::log(double)   the correctly rounded result for each input
//   fixy::fp::log(float)    the correctly rounded result for each input
//
// Each function is total.  ±0 gives -inf, +inf gives +inf, 1 gives +0, and a
// negative value (-inf and each negative subnormal value too) gives the
// canonical quiet NaN quiet_nan_of<T>, as does a NaN.  No function reads or
// writes errno or a flag of the floating-point unit on purpose, and no caller
// can read a flag of them.
//
// Mode of the register: a precondition
//   Each function assumes the default mode of the floating-point unit: the
//   round to nearest with ties to even, no flush of a subnormal result to
//   zero, and no read of a subnormal input as zero.  The proof of the correct
//   round below holds only in that mode, and another mode changes the bits.
//   A kernel scope proves the mode through its FpScope token
//   (foundation/os/FpControl.h).  A caller with no token states the mode in a
//   comment at its call.  PyTorch can set the flush of subnormal values in the
//   vessel process.  Then the slow path of log(float) reads a subnormal input
//   as zero in its conversion to double, and gives -inf.  A device backend
//   that reads subnormal values as zero cannot take these bodies for a
//   subnormal input.
//
// Operations
//   As in fixy/fp/Exp.h: an add, a subtract or a multiply of IEEE 754, a
//   fused multiply-add of detail::fma_of_finite, an integer operation on the
//   bits, a read of a table, or a conversion between float and double, in the
//   order of the source, under the floor crucible_fp_strict
//   (-ffp-contract=off).  Each fma rounds one time on each target.  The
//   functions are in the inline namespace CRUCIBLE_SIMD_TARGET, so a program
//   can link the bodies of two levels and compare their results.
//
// Algorithm of log(double): the reduction of the table method of ARM with the
// inverses of 10 bits of CORE-MATH, a rounding test of Ziv, and a slow path in
// three doubles.  u = 2^-53, and RN is the round to nearest.
//   x = 2^e y with y in [0x1.6ap-1, 0x1.6ap0).  t = bits(x) - bits(0x1.6ap-1)
//     holds e in its top 12 bits with the sign, and the row i in its next 9
//     bits, and bits(x) less e in the exponent gives y.  A subnormal x takes
//     a shift of its bits that puts its leading bit at bit 52, so |e| <= 1074.
//   The 512 rows split the range of y into steps of 2^-10 below 1 and of 2^-9
//     above 1.  r_i has 10 or fewer significant bits, and r_i = 1 for the
//     three rows from 1 - 2^-9 to 1 + 2^-9.  So z = fma(r_i, y, -1) is exact,
//     with |z| <= 2^-9.00.  -log(r_i) = l1 + l2 + l3 in three doubles, l1 a
//     multiple of 2^-42.  ln2 has the same form: ln2_high (42 bits) +
//     ln2_low, or ln2_high + ln2_middle (40 bits) + ln2_third.
//   The fast path, for each x out of the path near 1:
//     A = e ln2_high + l1 is exact: two multiples of 2^-42 below 2^10.
//     (h, l) = FastTwoSum(A, z), because |A| > |z|: for e = 0, the |l1| of
//       each row is at least 1.98 times each |z| of the row.
//     p = z^2 (P2 + z (P3 + P4 z + z^2 (P5 + P6 z))), the minimax polynomial
//       of log1p(z) - z for the absolute error.
//     l = RN(p + RN(e ln2_low + RN(l + l2))), the middle step as one fma.
//       The sum of the low parts does not wait for p.
//     Then |h + l - log(x)| <= eps_fast = 2^-69.43, absolute.
//     The rounding test: with E = 2^-68.5, if RN(h + RN(l + E)) and
//     RN(h + RN(l - E)) are the same value, that value is the correctly
//     rounded result, because RN is monotone and log(x) is between the two
//     sums.  Each sum takes an inner round of at most u |l + E|, so the test
//     needs a margin of 2^-69.20.
//   The path near 1, for 1 - 2^-9 <= x < 1 + 2^-9: r = x - 1 is exact.
//     (q_hi, q_lo) = TwoProduct(r, r), R = B1 + r (B2 + r (B3 + r (B4 +
//     r B5))), c = RN(RN(r q_hi) R), (h, l0) = FastTwoSum(r, -q_hi/2), and
//     l = RN(l0 + RN(-q_lo/2 + c)).  Then |h + l - log(x)| <= eps_near
//     |log(x)|, with eps_near = 2^-69.41.  The rounding test takes E = 2^-69 r,
//     and it needs 2^-69.25 |r|.
//   The slow path computes log(x) in three doubles with a relative error
//     eps_acc = 2^-137.16, and rounds that sum to nearest in each bit.
//     k2 = round(z 2^16) through the add of 0x1.8p52, |k2| <= 128, and r2 is
//     the double nearest 1 / (1 + k2 2^-16).  z2 = (r2 - 1) + r2 z = z_hi +
//     w + t exactly, from a TwoProduct and three TwoSums: the last one gives
//     the pair (z_hi, w), and t is the error of the second.  |z2| <= 2^-17.00.
//     log(x) = e ln2 - log(r_i) - log(r2) + log1p(z2), with ln2 and each -log
//     in three doubles.  log1p(z2) = z2 - z2^2/2 + f(z2), f(z) = z^3 Q(z),
//     with the Taylor polynomial of degree 9: 1/3 and 1/5 in two doubles,
//     -1/4 and the terms of degree 6 to 9 in one double.  f(z_hi + w) comes
//     from a Horner sum in pairs of doubles, and z2^2/2 from exact products.
//     The term RN(z_hi^2) t puts back f(z2) - f(z_hi + w), which is
//     t xi^2 / (1 + xi) for a xi between the two.  13 parts go through a
//     TwoSum into h and a TwoSum of its error into m, and 7 small parts go
//     into l, so only the adds of l round.  The round to nearest is
//     symmetric, so the round of h + m + l takes their absolute values.
//
// Error budget of the fast path, from `fp_tables.py log` (exact fractions)
//   R = 2^-9.00 bounds |z|.  The absolute errors of h + l:
//     the polynomial, a bound for each |z| <= R    2^-71.04
//     the rounds of the polynomial                 2^-70.41
//     -log(r_i) - l1 - l2                          2^-97.01
//     e (ln2 - ln2_high - ln2_low), |e| <= 1075    2^-91.95
//     the round of l + l2                          2^-95.42
//     the round of e ln2_low + (l + l2)            2^-86.97
//     the round of p + (...)                       2^-72.00
//     the sum: eps_fast                            2^-69.43
//
// Error budget of the path near 1, from `fp_tables.py log` (exact fractions)
//   The rounding errors, relative to |r| <= 2^-9:
//     the rounds of R, times r^2                   2^-72.58
//     the rounds of r q_hi and of c                2^-71.00
//     the round of -q_lo/2 + c                     2^-72.58
//     the round of l0 + (...)                      2^-72.58
//   The polynomial, relative to |log(x)|           2^-70.99
//   The sum, relative to |log(x)|: eps_near        2^-69.41
//
// Error budget of the slow path, from `fp_tables.py log` (exact fractions)
//   The double-word operations of fixy/fp/DoubleWord.h take the bounds of
//   Joldes, Muller and Popescu (ACM Transactions on Mathematical Software
//   44(2), 2017): 2u^2 for a product with a double (algorithm 9),
//   3u^2 / (1 - 4u) for an add of two pairs (algorithm 6), and 5u^2 for a
//   product of two pairs (algorithm 12).  The errors relative to |z2|:
//     f(z_hi + w), the rounds of the pairs         2^-137.26
//     f(z_hi + w), H on z_hi only                  2^-157.79
//     the round of w^2/2                           2^-177.00
//     the polynomial, a bound for each |z2| <= R2  2^-141.25
//   |t| <= 2^-114.99, and |t| <= 2^-105.00 |z| when r_i = 1 and k2 != 0.  The
//   errors in t, the rounds of z_hi t and of RN(z_hi^2) t, the parts that
//   z2^2/2 drops and the rest of f(z2) - f(z_hi + w), are at most 2^-50.99 |t|.
//   The tables: -log(r_i) to 2^-151.01, -log(r2) to 2^-171.01, and ln2 to
//   2^-140.33.  Each partial sum h is at most 2^1.00 |log(x)|, so |l| <=
//   2^-97.58 |log(x)|, and the 20 rounds of l cost 2^-146.26 |log(x)|.
//   The relative error in each case:
//     r_i = 1, e = 0, k2 = 0: z2 = z, t = 0        2^-137.16
//     r_i = 1, e = 0, k2 != 0: |z| > 2^-17         2^-137.16
//     r_i != 1, e = 0: |log(x)| > 1/513            2^-141.79
//     e != 0: |log(x)| >= |e| / 2.88628            2^-138.79
//     the largest: eps_acc                         2^-137.16
//   A relative error e gives a distance of at most 2e between the mantissas,
//   so the slow path is at most 2^-136.16 from log(x) in the mantissa.
//
// Coverage of the correct round
//   The round of the slow path is correct for x if no boundary of the round
//   is between log(x) and the sum of the three doubles.  So each range of x
//   needs a bound of the distance of log(x) to the nearest midpoint, larger
//   than the error of the path.  A list of hard cases bounds the distance of
//   each input off the list.  log(x) is a double only for x = 1, so no other
//   input is an exact case.
//     Normal inputs: Lefevre and Muller, "Worst cases for correct rounding of
//       the elementary functions in double precision", ARITH 15 (2001),
//       property 2: a mantissa distance of at most 2^-117 gives the correct
//       round in each of the four modes, for each normal double x.  A margin
//       of 19.16 bits.
//     Normal inputs, second source: the list log.wc of CORE-MATH.  Its block
//       from the list of Lefevre has 6348 inputs, and the hardest is
//       0x1.fd15daa6ce332p+732, 2^-62.03 units in the last place, a relative
//       2^-115.02: the hardest of the whole list, a margin of 22.14 bits.  Its
//       block of the BaCSeL search at -m 48 has 128590 inputs, with the
//       sentence "we should have all worst cases with 47 identical bits or
//       more in this file", and its hardest input is 2^-56.89 units.  Each
//       normal binade holds 30 or more inputs of the list, about 64 expected,
//       and up to 2038 near 1.
//     Subnormal inputs: Lefevre and Muller ignore the subnormal format.  The
//       source is the published BaCSeL search at -m 48 of log.wc, with its
//       own sentence above.  The positive control: its block holds the
//       subnormal inputs in normalized form, 20, 14, 9, 6 and 2 in the
//       binades -1022 to -1026, against about 32, 16, 8, 4 and 2 expected,
//       and 1 in each of the binades -1028, -1030 and -1073.  The open
//       confirmation is an own pinned BaCSeL run on the subnormal binades at
//       -m 48.
//
// Algorithm of log(float)
//   The fast path computes in double.  x = 2^k z with z in [0x1.67p-1,
//   0x1.67p0) and 128 intervals.  invc_i has 29 significant bits, and the
//   interval that holds 1 has invc = 1 and logc = 0.  z has 24 bits, so
//   z * invc_i is exact in double, and r = z * invc_i - 1 is exact.
//   log(x) = (fma(k, ln2, logc_i) + r) + r^2 P(r), P of degree 3, with a
//   relative error of the polynomial of 2^-46.6.  The rounding test and the
//   slow path are those of exp(float) in fixy/fp/Exp.h, with log(double) in the
//   slow path.  The fma sites of the fast path: one for k ln2, three in the
//   polynomial, one for the sum, and one for r on a target with an fma
//   instruction.
//   The exhaustive sweep of the 2^32 inputs against CORE-MATH proves the
//   correct round of each input (the target bench_fp_sweep).
//
// Tables
//   `python3 utils/tools/fp_tables.py log` prints the error budgets above and
//   each constant and table of log(double) with the pinned mpmath.  The
//   block of fixy/fp/LogTables.h is the end of its output, line for line.
//   `python3 utils/tools/fp_tables.py logf` prints those of log(float).

#include <foundation/Platform.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <fixy/fp/DoubleWord.h>
#include <fixy/fp/FmaOfFinite.h>
#include <fixy/fp/LogTables.h>
#include <fixy/fp/RoundToFloat.h>

#include <cstdint>

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "fixy/fp/Log.h needs the floor crucible_fp_strict (-ffp-contract=off). Without it the compiler can fuse a multiply and an add, and the bits change with the level."
#endif

namespace fixy::fp::detail {

// The bits of 1 - 2^-9 and 1 + 2^-9: the path near 1 takes each x from the
// first to below the second.
inline constexpr std::uint64_t kLogNearLow = 0x3FEFF00000000000ULL;
inline constexpr std::uint64_t kLogNearHigh = 0x3FF0080000000000ULL;
// The bits of 0x1.6ap-1, the start of the range of y.  A step of 2^43 in the
// bits of y is a row of kLogFirst: 2^-10 below 1 and 2^-9 above 1.
inline constexpr std::uint64_t kLogOffset = 0x3FE6A00000000000ULL;
// The mask of the row in the bits of x - kLogOffset, shifted down by 43.
inline constexpr std::uint64_t kLogRowMask = 511;
// The row that starts at y = 1.  It and the two rows below it hold the path
// near 1, with r = 1 and -log(r) = 0.
inline constexpr std::uint64_t kLogFirstOne = 300;
// The row of k2 = 0 in kLogSecond.
inline constexpr std::int32_t kLogSecondCenter = 128;
// The add of this value rounds a double of magnitude below 2^51 to an
// integer, and the low 32 bits of its result hold that integer.
inline constexpr double kLogShift = 0x1.8p52;
// The margin of the rounding test of the fast path, 2^-68.5 rounded to
// nearest, and the factor of |r| in the margin of the path near 1.
inline constexpr double kLogMargin = 0x1.6a09e667f3bcdp-69;
inline constexpr double kLogNearMargin = 0x1p-69;

inline constexpr int kLogFloatTableBits = 7;
// The bits of the float 0x1.67p-1: the start of the range of z.  In the bits,
// 1 is at the middle of interval 76, which has invc = 1 and logc = 0.
inline constexpr std::uint32_t kLogFloatOffset = 0x3F338000u;
inline constexpr double kLogFloatLn2 = 0x1.62e42fefa39efp-1;

// The minimax polynomial of log1p(r) - r = r^2 (A0 + A1 r + A2 r^2 + A3 r^3)
// for the relative error of log1p(r), with an error of 2^-46.58 for |r| up
// to 2^-8.
inline constexpr double kLogFloatA0 = -0x1.ffffffffcab9dp-2;
inline constexpr double kLogFloatA1 = 0x1.55555554f59c3p-2;
inline constexpr double kLogFloatA2 = -0x1.0000d543d7f2cp-2;
inline constexpr double kLogFloatA3 = 0x1.999b7d9f0dcf5p-3;

// The intervals of z of the float form, in two tables: 1/c with 29
// significant bits, and -log of it as a double.  A table of doubles takes
// its index with the scale 8 of the address, so the load needs no multiply
// of the index.  From python3 utils/tools/fp_tables.py logf.
// clang-format off
inline constexpr double kLogFloatInvc[1 << kLogFloatTableBits] = {
    0x1.6c16c17000000p+0, 0x1.6a13cd1000000p+0, 0x1.6816817000000p+0, 0x1.661ec6a000000p+0,
    0x1.642c859000000p+0, 0x1.623fa77000000p+0, 0x1.6058160000000p+0, 0x1.5e75bb9000000p+0,
    0x1.5c9882c000000p+0, 0x1.5ac056b000000p+0, 0x1.58ed231000000p+0, 0x1.571ed3c000000p+0,
    0x1.5555555000000p+0, 0x1.5390949000000p+0, 0x1.51d07eb000000p+0, 0x1.5015015000000p+0,
    0x1.4e5e0a7000000p+0, 0x1.4cab887000000p+0, 0x1.4afd6a0000000p+0, 0x1.49539e4000000p+0,
    0x1.47ae148000000p+0, 0x1.460cbc8000000p+0, 0x1.446f865000000p+0, 0x1.42d6626000000p+0,
    0x1.4141414000000p+0, 0x1.3fb0140000000p+0, 0x1.3e22cbd000000p+0, 0x1.3c995a4000000p+0,
    0x1.3b13b14000000p+0, 0x1.3991c2c000000p+0, 0x1.3813814000000p+0, 0x1.3698df4000000p+0,
    0x1.3521cfb000000p+0, 0x1.33ae45b000000p+0, 0x1.323e34a000000p+0, 0x1.30d1901000000p+0,
    0x1.2f684be000000p+0, 0x1.2e025c0000000p+0, 0x1.2c9fb4e000000p+0, 0x1.2b404ad000000p+0,
    0x1.29e412a000000p+0, 0x1.288b013000000p+0, 0x1.27350b9000000p+0, 0x1.25e2271000000p+0,
    0x1.2492492000000p+0, 0x1.2345679000000p+0, 0x1.21fb781000000p+0, 0x1.20b470c000000p+0,
    0x1.1f7047e000000p+0, 0x1.1e2ef3b000000p+0, 0x1.1cf06ae000000p+0, 0x1.1bb4a40000000p+0,
    0x1.1a7b961000000p+0, 0x1.1945381000000p+0, 0x1.1811812000000p+0, 0x1.16e0689000000p+0,
    0x1.15b1e5f000000p+0, 0x1.1485f0e000000p+0, 0x1.135c811000000p+0, 0x1.12358e7000000p+0,
    0x1.1111111000000p+0, 0x1.0fef011000000p+0, 0x1.0ecf56c000000p+0, 0x1.0db20a9000000p+0,
    0x1.0c97150000000p+0, 0x1.0b7e6ec000000p+0, 0x1.0a6810a000000p+0, 0x1.0953f39000000p+0,
    0x1.0842108000000p+0, 0x1.073260a000000p+0, 0x1.0624dd3000000p+0, 0x1.05197f8000000p+0,
    0x1.0410410000000p+0, 0x1.03091b5000000p+0, 0x1.0204081000000p+0, 0x1.0101010000000p+0,
    0x1.0000000000000p+0, 0x1.fc07f02000000p-1, 0x1.f81f820000000p-1, 0x1.f44659e000000p-1,
    0x1.f07c1f0000000p-1, 0x1.ecc07b3000000p-1, 0x1.e9131ac000000p-1, 0x1.e573ac9000000p-1,
    0x1.e1e1e1e000000p-1, 0x1.de5d6e4000000p-1, 0x1.dae6077000000p-1, 0x1.d77b655000000p-1,
    0x1.d41d41d000000p-1, 0x1.d0cb58f000000p-1, 0x1.cd85689000000p-1, 0x1.ca4b305000000p-1,
    0x1.c71c71c000000p-1, 0x1.c3f8f02000000p-1, 0x1.c0e0704000000p-1, 0x1.bdd2b8a000000p-1,
    0x1.bacf915000000p-1, 0x1.b7d6c3e000000p-1, 0x1.b4e81b5000000p-1, 0x1.b203640000000p-1,
    0x1.af286bd000000p-1, 0x1.ac5701b000000p-1, 0x1.a98ef60000000p-1, 0x1.a6d01a7000000p-1,
    0x1.a41a41a000000p-1, 0x1.a16d3f9000000p-1, 0x1.9ec8e95000000p-1, 0x1.9c2d14f000000p-1,
    0x1.999999a000000p-1, 0x1.970e4f8000000p-1, 0x1.948b0fd000000p-1, 0x1.920fb4a000000p-1,
    0x1.8f9c190000000p-1, 0x1.8d3018d000000p-1, 0x1.8acb90f000000p-1, 0x1.886e5f1000000p-1,
    0x1.8618618000000p-1, 0x1.83c977b000000p-1, 0x1.8181818000000p-1, 0x1.7f405fd000000p-1,
    0x1.7d05f41000000p-1, 0x1.7ad2209000000p-1, 0x1.78a4c81000000p-1, 0x1.767dce4000000p-1,
    0x1.745d174000000p-1, 0x1.724287f000000p-1, 0x1.702e05c000000p-1, 0x1.6e1f76b000000p-1,
};
inline constexpr double kLogFloatLogc[1 << kLogFloatTableBits] = {
    -0x1.68ac83f4c6a14p-2, -0x1.630030a4eac49p-2, -0x1.5d5bde0c15f30p-2, -0x1.57bf752e0d1fbp-2,
    -0x1.522ae0718a3d8p-2, -0x1.4c9e09e132c3cp-2, -0x1.4718dc171c41bp-2, -0x1.419b42461e8c7p-2,
    -0x1.3c25278733184p-2, -0x1.36b6776ba1117p-2, -0x1.314f1e34b5ce4p-2, -0x1.2bef07bec9354p-2,
    -0x1.269621034db92p-2, -0x1.214456d32b8d4p-2, -0x1.1bf9963b26b95p-2, -0x1.16b5ccba8fb73p-2,
    -0x1.1178e8197e47cp-2, -0x1.0c42d66ed62e3p-2, -0x1.071385f4d5862p-2, -0x1.01eae5716c691p-2,
    -0x1.f991c6eb3b379p-3, -0x1.ef5ade51cffe6p-3, -0x1.e530efd771012p-3, -0x1.db13db1e48940p-3,
    -0x1.d1037f1e55e7bp-3, -0x1.c6ffbc8f00f71p-3, -0x1.bd08738ebd8adp-3, -0x1.b31d8543bce3dp-3,
    -0x1.a93ed3e8ad9e3p-3, -0x1.9f6c406689664p-3, -0x1.95a5ae037017fp-3, -0x1.8beafec18fe8cp-3,
    -0x1.823c16431a3c2p-3, -0x1.7898d82fc4c73p-3, -0x1.6f0128a556abcp-3, -0x1.6574ebd44133ap-3,
    -0x1.5bf406dd43db2p-3, -0x1.527e5e2a1b58dp-3, -0x1.4913d8693b561p-3, -0x1.3fb45a59128ccp-3,
    -0x1.365fcb0d59016p-3, -0x1.2d1610fbe813ap-3, -0x1.23d712db9c202p-3, -0x1.1aa2b819bf72ap-3,
    -0x1.1178e8027e47cp-3, -0x1.08598b8663a07p-3, -0x1.fe89137fbd566p-4, -0x1.ec7397d4a1120p-4,
    -0x1.da727670446a2p-4, -0x1.c8857fe2c4b23p-4, -0x1.b6ac892ed5b1cp-4, -0x1.a4e763cb1bc38p-4,
    -0x1.9335e5bd94989p-4, -0x1.8197e35d0e3f0p-4, -0x1.700d3122ac0e1p-4, -0x1.5e95a49c791cbp-4,
    -0x1.4d31156607eacp-4, -0x1.3bdf5a731ee64p-4, -0x1.2aa04a12717a5p-4, -0x1.1973bcbd65567p-4,
    -0x1.08598b49e3a07p-4, -0x1.eea31c026b87cp-5, -0x1.ccb73d0ddb2ccp-5, -0x1.aaef2de5b10fcp-5,
    -0x1.894aa1c9fb343p-5, -0x1.67c94ee54bb58p-5, -0x1.466aec7ade3e9p-5, -0x1.252f32f6d183fp-5,
    -0x1.0415d81e74444p-5, -0x1.c63d2da94aaf1p-6, -0x1.849252c48cabfp-6, -0x1.432a92f980cc1p-6,
    -0x1.0205648935847p-6, -0x1.824489408a2aap-7, -0x1.010157488de71p-7, -0x1.0080549588b35p-8,
    0.0, 0x1.fe02a69106789p-8, 0x1.fc0a890fc03e4p-7, 0x1.7b91b1155b11bp-6,
    0x1.f829b1e783300p-6, 0x1.39e87ba1ebd60p-5, 0x1.77458f532dcfcp-5, 0x1.b42dd713971bfp-5,
    0x1.f0a30c21162a6p-5, 0x1.16536ee637ae1p-4, 0x1.341d793bbd1d1p-4, 0x1.51b073c96183fp-4,
    0x1.6f0d28d256b4cp-4, 0x1.8c345da019b21p-4, 0x1.a926d3a6ad563p-4, 0x1.c5e5492abc743p-4,
    0x1.e2707722af2e6p-4, 0x1.fec912fbbeabbp-4, 0x1.0d77e7a908e59p-3, 0x1.1b72ad33f67a0p-3,
    0x1.29552f6fff523p-3, 0x1.371fc1f6e8f74p-3, 0x1.44d2b6c5b7d1ep-3, 0x1.526e5e5a1b438p-3,
    0x1.5ff306ee793d4p-3, 0x1.6d60fe601d21dp-3, 0x1.7ab890410d909p-3, 0x1.87fa06438c911p-3,
    0x1.9525a9e3456b4p-3, 0x1.a23bc223ab563p-3, 0x1.af3c94ed0bff3p-3, 0x1.bc28673a58cd6p-3,
    0x1.c8ff7c59a9a22p-3, 0x1.d5c216b8fbb91p-3, 0x1.e27076d5af2e6p-3, 0x1.ef0adcaec5936p-3,
    0x1.fb9186b5e3e2bp-3, 0x1.040259530d041p-2, 0x1.0a324e38b90e3p-2, 0x1.1058bf8d24ad5p-2,
    0x1.1675cacaba60ep-2, 0x1.1c898c09d99fbp-2, 0x1.22941fc0f7966p-2, 0x1.2895a13e286a3p-2,
    0x1.2e8e2bc311d31p-2, 0x1.347dd9a447d55p-2, 0x1.3a64c56b145eap-2, 0x1.404308716a7e4p-2,
    0x1.4618bc31c5ec2p-2, 0x1.4be5f963b78a1p-2, 0x1.51aad874df82dp-2, 0x1.5767718015a6cp-2,
};
// clang-format on

// The tolerance of the rounding test of the float fast path, in units of the
// last bit of the double result: an error of 2^-43 times 2^53.  The error of
// the fast path is at most about 2^-46.5 relative to the result, from the
// polynomial and three rounds, so the margin is about eleven.
inline constexpr std::uint64_t kLogFloatTolerance = std::uint64_t{1} << 10;

// The reduction of a positive finite x = 2^e y, with y in [0x1.6ap-1, 0x1.6ap0).
struct LogReduction {
    // z = r_i y - 1, exact.
    double reduced = 0.0;
    // The row of the interval i of y in kLogFirst.
    std::uint64_t position = 0;
    // e as a double.
    double scale = 0.0;
};

// The three doubles of the sum of the slow path.
struct LogSum {
    double high = 0.0;
    double middle = 0.0;
    double low = 0.0;
};

inline namespace CRUCIBLE_SIMD_TARGET {

// The bits of a positive subnormal x with its leading bit at bit 52.  The
// biased exponent 1 - shift is zero or negative, and it wraps modulo 2^64, so
// a reader takes it through an arithmetic shift.
[[nodiscard, gnu::always_inline]] constexpr std::uint64_t log_normal_bits_(std::uint64_t bits) noexcept {
    auto const shift = static_cast<std::uint64_t>(__builtin_clzll(bits) - 11);
    return (bits << shift) - (shift << 52);
}

// The reduction of the bits of a positive finite x, after log_normal_bits_
// for a subnormal x, in the form of the table method of ARM.  t = bits(x) -
// kLogOffset holds e in its top 12 bits with the sign, and the row in its
// next 9 bits.  The subtract of e from the exponent of x gives y.  The row
// has an r_i of 10 or fewer significant bits, so the fma is exact.  Each step
// wraps modulo 2^64, so the wrapped exponent of a subnormal x gives e too.
[[nodiscard, gnu::always_inline]] constexpr LogReduction log_reduce_(std::uint64_t bits) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    std::uint64_t const offset_bits = bits - kLogOffset;
    std::uint64_t const position = (offset_bits >> 43) & kLogRowMask;
    std::int64_t const exponent = static_cast<std::int64_t>(offset_bits) >> 52;
    double const fraction = __builtin_bit_cast(double, bits - (offset_bits & (std::uint64_t{0xfff} << 52)));
    double const reduced = fma_of_finite(kLogFirst[position].inverse, fraction, -1.0);
    return LogReduction{reduced, position, static_cast<double>(exponent)};
}

// Add a part to the sum of the slow path: a TwoSum into high, and a TwoSum of
// its error into middle.  Only the add of the second error to low rounds.
[[nodiscard, gnu::always_inline]] constexpr LogSum log_sum_add_(LogSum sum, double part) noexcept {
    DoubleWord const top = two_sum_(sum.high, part);
    DoubleWord const next = two_sum_(sum.middle, top.low);
    return LogSum{top.high, next.high, sum.low + next.low};
}

// log(x) in three doubles for each positive finite x, with the relative error
// eps_acc of the comment of the header.  The three doubles do not overlap:
// high is the round of high + middle, and middle the round of middle + low.
[[nodiscard, gnu::always_inline]] constexpr LogSum log_accurate_sum_(double value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    std::uint64_t bits = __builtin_bit_cast(std::uint64_t, value);
    if ((bits >> 52) == 0) bits = log_normal_bits_(bits);
    LogReduction const reduction = log_reduce_(bits);
    LogEntry const& first = kLogFirst[reduction.position];
    double const scale = reduction.scale;
    // k2 = round(z 2^16) from the low bits of the add of the shift, |k2| <= 128.
    std::uint64_t const shifted_bits = __builtin_bit_cast(std::uint64_t, reduction.reduced * 0x1p16 + kLogShift);
    auto const steps = static_cast<std::int32_t>(static_cast<std::uint32_t>(shifted_bits));
    LogEntry const& second = kLogSecond[static_cast<std::uint64_t>(steps + kLogSecondCenter)];
    // z2 = (r2 - 1) + r2 z = point.high + point.low + point_third exactly: r2 - 1
    // is exact, and each step below is an exact product or sum.
    DoubleWord const product = two_product_(second.inverse, reduction.reduced);
    DoubleWord const head = two_sum_(second.inverse - 1.0, product.high);
    DoubleWord const rest = two_sum_(head.low, product.low);
    DoubleWord const point = two_sum_(head.high, rest.high);
    double const point_third = rest.low;
    // Q = 1/3 - z/4 + z^2/5 + z^3 H(z): H of degree 3 in doubles on the high
    // part of z2, then a Horner sum in pairs of doubles.
    double horner = fma_of_finite(point.high, kLogSlowC9, kLogSlowC8);
    horner = fma_of_finite(point.high, horner, kLogSlowC7);
    horner = fma_of_finite(point.high, horner, kLogSlowC6);
    DoubleWord sum = dw_plus_dw_(DoubleWord{kLogSlowC5High, kLogSlowC5Low}, dw_times_fp_(point, horner));
    sum = dw_plus_dw_(DoubleWord{kLogSlowC4, 0.0}, dw_times_dw_(point, sum));
    sum = dw_plus_dw_(DoubleWord{kLogSlowC3High, kLogSlowC3Low}, dw_times_dw_(point, sum));
    DoubleWord const curve = dw_times_dw_(dw_times_dw_(dw_times_dw_(point, point), point), sum);
    // z2^2/2 from the exact products of the high part, and e ln2_third as an
    // exact pair.
    DoubleWord const square = two_product_(point.high, point.high);
    DoubleWord const cross = two_product_(point.high, point.low);
    DoubleWord const exponent_third = two_product_(scale, kLogLn2Third);
    // log(x) = e ln2 - log(r_i) - log(r2) + z2 - z2^2/2 + z2^3 Q.  The first
    // part is exact: two multiples of 2^-42 below 2^10.
    LogSum total{scale * kLogLn2High + first.high, 0.0, 0.0};
    total = log_sum_add_(total, second.high);
    total = log_sum_add_(total, point.high);
    total = log_sum_add_(total, scale * kLogLn2Middle);
    total = log_sum_add_(total, first.middle);
    total = log_sum_add_(total, square.high * -0.5);
    total = log_sum_add_(total, curve.high);
    total = log_sum_add_(total, second.middle);
    total = log_sum_add_(total, point.low);
    total = log_sum_add_(total, exponent_third.high);
    total = log_sum_add_(total, first.low);
    total = log_sum_add_(total, square.low * -0.5);
    total = log_sum_add_(total, -cross.high);
    total = log_sum_add_(total, curve.low);
    // The small parts: the third parts of -log(r2) and of z2, the parts z_hi t,
    // the low part of z_hi w and w^2/2 of z2^2/2, the part z_hi^2 t of z2^3 Q
    // that the cube of z_hi + w drops, and the low part of e ln2_third.
    double low = total.low + second.low;
    low = low + point_third;
    low = low - point.high * point_third;
    low = low - cross.low;
    low = low - (point.low * point.low) * 0.5;
    low = low + square.high * point_third;
    low = low + exponent_third.low;
    // |m| is far below |h|, so the FastTwoSum is exact.
    DoubleWord const top = fast_two_sum_(total.high, total.middle);
    DoubleWord const tail = two_sum_(top.low, low);
    return LogSum{top.high, tail.high, tail.low};
}

// The slow path: log(x) correctly rounded for each positive finite x.  It
// rounds the three doubles of log_accurate_sum_.  The coverage table of the
// comment of the header shows that no boundary of the round is as close to
// log(x) as their error.
[[gnu::cold, gnu::noinline, nodiscard]] constexpr double log_accurate_(double value) noexcept {
    LogSum const sum = log_accurate_sum_(value);
    // The round to nearest is symmetric, so the round of the three doubles
    // takes their absolute values.
    double const sign = sum.high < 0.0 ? -1.0 : 1.0;
    return sign * round_triple_(sign * sum.high, sign * sum.middle, sign * sum.low);
}

// h + l of the fast path, for a positive finite x out of the path near 1,
// with the error eps_fast of the comment of the header.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord log_pair_(LogReduction reduction) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    LogEntry const& entry = kLogFirst[reduction.position];
    double const reduced = reduction.reduced;
    double const scale = reduction.scale;
    // e ln2_high + l1 is exact: two multiples of 2^-42 below 2^10.  Its
    // magnitude is above |z|, as |l1| >= 2^-8.41 > |z| for e = 0.
    DoubleWord const head = fast_two_sum_(scale * kLogLn2High + entry.high, reduced);
    double const square = reduced * reduced;
    double const upper_pair = fma_of_finite(reduced, kLogP6, kLogP5);
    double const lower_pair = fma_of_finite(reduced, kLogP4, kLogP3);
    double polynomial = fma_of_finite(square, upper_pair, lower_pair);
    polynomial = fma_of_finite(reduced, polynomial, kLogP2);
    polynomial = polynomial * square;
    // The sum of the low parts does not wait for the polynomial, so its add is
    // the one step after the polynomial.
    double const rest = fma_of_finite(scale, kLogLn2Low, head.low + entry.middle);
    return DoubleWord{head.high, polynomial + rest};
}

// h + l of the path near 1, for 1 - 2^-9 <= x < 1 + 2^-9, with the error
// eps_near of the comment of the header.  r = x - 1 is exact, and r - r^2/2
// is an exact pair.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord log_near_pair_(double value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    double const reduced = value - 1.0;
    DoubleWord const square = two_product_(reduced, reduced);
    double tail = fma_of_finite(reduced, kLogB5, kLogB4);
    tail = fma_of_finite(reduced, tail, kLogB3);
    tail = fma_of_finite(reduced, tail, kLogB2);
    tail = fma_of_finite(reduced, tail, kLogB1);
    double const curve = (reduced * square.high) * tail;
    // |r| >= r^2/2, so the FastTwoSum is exact.
    DoubleWord const head = fast_two_sum_(reduced, square.high * -0.5);
    return DoubleWord{head.high, head.low + (square.low * -0.5 + curve)};
}

// The path near 1: the pair, and the rounding test with the margin 2^-69 r.
[[nodiscard, gnu::always_inline]] constexpr double log_near_(double value) noexcept {
    DoubleWord const pair = log_near_pair_(value);
    double const margin = (value - 1.0) * kLogNearMargin;
    double const upper = pair.high + (pair.low + margin);
    double const lower = pair.high + (pair.low - margin);
    if (__builtin_islessgreater(upper, lower)) [[unlikely]]
        return log_accurate_(value);
    return upper;
}

// The midpoint inputs of the float form: each input whose log(double) is a
// float midpoint, with its correctly rounded result from the sweep of the
// oracle.  A midpoint input that is not in the list takes the round to even,
// and the sweep proves that no such input exists.
[[gnu::cold, nodiscard]] constexpr float log_float_midpoint_(std::uint32_t input_bits, double result) noexcept {
    switch (input_bits) {
        case 0x41178FEBu:
            return __builtin_bit_cast(float, 0x400FE5E7u);
        case 0x4C5D65A5u:
            return __builtin_bit_cast(float, 0x418F034Bu);
        case 0x3C413D3Au:
            return __builtin_bit_cast(float, 0xC08E158Fu);
        case 0x65D890D3u:
            return __builtin_bit_cast(float, 0x4254D1F9u);
        case 0x6F31A8ECu:
            return __builtin_bit_cast(float, 0x42845A89u);
        default:
            return static_cast<float>(result);
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr double log(double value) noexcept {
    std::uint64_t bits = __builtin_bit_cast(std::uint64_t, value);
    if (bits - detail::kLogNearLow < detail::kLogNearHigh - detail::kLogNearLow) return detail::log_near_(value);
    std::uint64_t const top = bits >> 48;
    if (top - 0x0010u >= 0x7ff0u - 0x0010u) [[unlikely]] {
        // x below 2^-1022, negative, an infinity or a NaN.
        if ((bits << 1) == 0) return -::foundation::core::infinity_of<double>;
        if (bits == 0x7ff0000000000000ULL) return value;
        if ((top & 0x8000u) != 0 || (top & 0x7ff0u) == 0x7ff0u) return ::foundation::core::quiet_nan_of<double>;
        bits = detail::log_normal_bits_(bits);
    }
    detail::DoubleWord const pair = detail::log_pair_(detail::log_reduce_(bits));
    double const upper = pair.high + (pair.low + detail::kLogMargin);
    double const lower = pair.high + (pair.low - detail::kLogMargin);
    if (__builtin_islessgreater(upper, lower)) [[unlikely]]
        return detail::log_accurate_(value);
    return upper;
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp

namespace fixy::fp::detail {

inline namespace CRUCIBLE_SIMD_TARGET {

// The slow path of the float form: the round of log(double), with the list of
// the midpoint inputs.
[[gnu::cold, gnu::noinline, nodiscard]] constexpr float log_float_slow_(float value) noexcept {
    double const result = ::fixy::fp::log(static_cast<double>(value));
    if (is_float_midpoint_(result)) [[unlikely]] {
        return log_float_midpoint_(__builtin_bit_cast(std::uint32_t, value), result);
    }
    return static_cast<float>(result);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr float log(float value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    std::uint32_t bits = __builtin_bit_cast(std::uint32_t, value);
    if (bits - 0x00800000u >= 0x7f800000u - 0x00800000u) [[unlikely]] {
        // x below 2^-126, negative, an infinity or a NaN.
        if ((bits << 1) == 0) return -::foundation::core::infinity_of<float>;
        if (bits == 0x7f800000u) return value;
        if ((bits >> 31) != 0 || bits > 0x7f800000u) return ::foundation::core::quiet_nan_of<float>;
        // A positive subnormal x: the same normalization as the double form.
        auto const shift = static_cast<std::uint32_t>(__builtin_clz(bits) - 8);
        bits = (bits << shift) - (shift << 23);
    }
    std::uint32_t const offset_bits = bits - detail::kLogFloatOffset;
    std::uint32_t const index =
        (offset_bits >> (23 - detail::kLogFloatTableBits)) & ((1u << detail::kLogFloatTableBits) - 1u);
    std::int32_t const exponent = static_cast<std::int32_t>(offset_bits) >> 23;
    float const reduced_input = __builtin_bit_cast(float, bits - (offset_bits & 0xff800000u));
    double const invc = detail::kLogFloatInvc[index];
    double const logc = detail::kLogFloatLogc[index];
    // z has 24 bits and invc has 29, so the product is exact, and the
    // subtract of 1 is exact.
    // So an fma gives the same exact r as the multiply and the subtract, two
    // cycles sooner.  At x86-64 and x86-64-v2 the fma is a form in doubles
    // of about 100 instructions, so the multiply and the subtract stay there.
#if defined(__FMA__) || defined(__aarch64__)
    double const reduced = fma_of_finite(static_cast<double>(reduced_input), invc, -1.0);
#else
    double const reduced = static_cast<double>(reduced_input) * invc - 1.0;
#endif
    double const whole = fma_of_finite(static_cast<double>(exponent), detail::kLogFloatLn2, logc);
    // A0 + A1 r + A2 r^2 + A3 r^3 in the form of Estrin, and whole + r beside
    // it: the chain is two fma deep after r^2.
    double const square = reduced * reduced;
    double const low = fma_of_finite(reduced, detail::kLogFloatA1, detail::kLogFloatA0);
    double const high = fma_of_finite(reduced, detail::kLogFloatA3, detail::kLogFloatA2);
    double const tail = fma_of_finite(square, high, low);
    double const result = fma_of_finite(square, tail, whole + reduced);
    if (detail::is_near_float_midpoint_(result, detail::kLogFloatTolerance)) [[unlikely]] {
        return detail::log_float_slow_(value);
    }
    return static_cast<float>(result);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp
