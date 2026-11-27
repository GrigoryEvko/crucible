#pragma once

// exp of a double and of a float, with the same bits on each target and each
// level of the instruction set.
//
//   fixy::fp::exp(double)   the correctly rounded result for each input
//   fixy::fp::exp(float)    the correctly rounded result for each input
//
// Each function is total.  A NaN gives the canonical quiet NaN quiet_nan_of<T>,
// so the bits do not depend on the payload rules of the host.  +inf gives
// +inf, -inf gives +0, and a result past the largest finite value gives +inf.
// A result below the smallest subnormal value gives +0.  No function reads or
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
//   vessel process, and then a subnormal result changes.  A device backend
//   that flushes subnormal values cannot take these bodies for an input with
//   a subnormal result.
//
// Operations
//   Each operation is an add, a subtract or a multiply of IEEE 754, a fused
//   multiply-add of detail::fma_of_finite (fixy/fp/FmaOfFinite.h), an integer
//   operation on the bits, a read of a table, or a conversion between float
//   and double.  The order of the operations is the order of the source.  The
//   build contracts no multiply and add (-ffp-contract=off, the floor
//   crucible_fp_strict), so each fma is an explicit fma_of_finite.  It rounds
//   one time on each target: the instruction at x86-64-v3, x86-64-v4 and
//   aarch64, a form in doubles at x86-64 and x86-64-v2.  A NaN input returns
//   quiet_nan_of<T> before the first fma, so no fma takes a NaN.  The
//   functions are in the inline namespace CRUCIBLE_SIMD_TARGET, so a program
//   can link the bodies of two levels and compare their results.
//
// Algorithm of exp(double): the table method of Tang, a rounding test of Ziv,
// and a slow path in three doubles.  u = 2^-53, and RN is the round to
// nearest.
//   k = round(x * 128 / ln2) through the add and the subtract of 0x1.8p52.  At
//     x86-64-v3, x86-64-v4 and aarch64 one fma gives x * 128/ln2 + 0x1.8p52
//     with one round.  The other levels take a multiply and an add, because
//     their fma is a form of about 100 instructions.  The two can give a
//     different k near a half.  Each k gives |r| <= (1/2 + 2^-35) ln2/128, and
//     each path gives the correctly rounded result for each such k.  So the
//     bits are the same on each level.
//   j = k mod 128, s = the double nearest 2^(j/128) with the exponent of
//     k/128 added to its bits, and 2^(j/128) = s (1 + tau), with |tau| <= u.
//     The table holds tail = RN(tau).
//   The fast path, for 2^-54 <= |x| < 512:
//     r1 = x - k L1 is exact: |k| < 2^17, and L1 has 36 bits.
//     r_hi = RN(r1 - k L2), and r_lo = RN(RN(r1 - r_hi) - k L2).
//     P' = C2 + C3 r_hi + r_hi^2 (C4 + C5 r_hi), and q = r_hi^2 P' +
//     tail (1 + r_hi) + r_lo (1 + r_hi).  r_lo (1 + r_hi) joins after the
//     polynomial, so r_lo is off the longest chain.
//     hi = RN(s + s r_hi), and lo = RN(s q + RN(s + s r_hi - hi)).
//     Then |hi + lo - exp(x)| <= eps_fast exp(x), with eps_fast = 2^-65.17.
//     The rounding test: with E = 2^-65 hi, if RN(hi + RN(lo + E)) and
//     RN(hi + RN(lo - E)) are the same value, that value is the correctly
//     rounded result, because RN is monotone and exp(x) is between the two
//     sums.  Each sum takes an inner round of at most u |lo + E|, so the test
//     needs a margin of eps_fast + u |lo| + u E <= 2^-65.15 |hi|.  About one
//     input in 3000 does not pass, and the slow path takes it.  |hi| is at
//     least 2^-739, so E is a normal double, and the round of a subnormal
//     part of the path costs at most 2^-1075, below 2^-336 |hi|.
//   The edge path, for |x| >= 512, an infinity and a NaN: a NaN gives the
//     canonical NaN, x above 0x1.62e42fefa39efp+9 gives +inf, x at or below
//     -0x1.74910d52d3052p+9 gives +0, and x below -0x1.6232bdd7abcd2p+9 (a
//     subnormal result) takes the slow path.  Each other x takes the fast
//     path with s scaled by 2^-1009 or 2^1022, so each value is normal, and
//     the result scales back with one exact multiply.
//   The slow path computes exp(x) / 2^(k >> 7) in three doubles with a
//     relative error eps_acc = 2^-120.19, and rounds that sum to nearest in
//     each bit.  r = x - k ln2/128 comes from four parts of ln2/128: the
//     first two have 35 bits, so their products with k (18 bits) are exact,
//     and the sum is exact to 2^-166.  exp(r) = 1 + r + r^2 Q(r), with the
//     Taylor polynomial of degree 11: 1/n! in two doubles to n = 6, in one
//     double after it.  Q comes from a Horner sum in pairs of doubles.
//     2^(j/128) = s (1 + tail + low), with a second table of the low parts.
//     A subnormal result adds 2^(-1022 - (k >> 7)) before the round, so the
//     sum rounds at the precision of the subnormal result, and the subtract
//     of that power after the round is exact.  The path takes a multiply and
//     an add for k at each level, so its bits are the same on each level.
//
// Error budget of the fast path, from `fp_tables.py exp` (exact fractions)
//   R = 2^-8.53 bounds |r_hi| and |r|.  The errors of U, with
//   exp(x) = s (1 + U), in absolute units:
//     the rounds of P' (times r_hi^2)              2^-70.06
//     the round of r_hi^2 (times P')               2^-71.06
//     the round of r_hi^2 P' + tail (1 + r_hi)     2^-71.06
//     the round of q                               2^-71.06
//     tau r_hi^2 P', which q drops                 2^-71.29
//     the second-order part of r_lo, dropped       2^-79.58
//     tau - tail, the rounds of tail (1 + r_hi)
//       and of r_lo (1 + r_hi)                     2^-106.2 or less
//     the sum, over (1 + tau) exp(r)               2^-68.50
//   The relative errors of hi + lo:
//     the polynomial, a bound for each |r| <= R    2^-65.35
//     the errors of U                              2^-68.50
//     the round of lo, with |lo| <= 2^-18.05 |hi|  2^-71.05
//     r - r_hi - r_lo, with |k| <= 2^17            2^-81.51
//     the round of the error of hi                 2^-106
//     the sum: eps_fast                            2^-65.17
//
// Error budget of the slow path, from `fp_tables.py exp` (exact fractions)
//   The double-word operations of fixy/fp/DoubleWord.h take the bounds of
//   Joldes, Muller and Popescu (ACM Transactions on Mathematical Software
//   44(2), 2017): 2u^2 for an add of a double (algorithm 4) and a product
//   with a double (algorithm 9), 3u^2 / (1 - 4u) for an add of two pairs
//   (algorithm 6), and 5u^2 for a product of two pairs (algorithm 12).
//     Q, the Horner sum                            2^-105.66 absolute
//     r^2 in a pair of doubles                     2^-120.25 absolute
//     w = r^2 Q in a pair of doubles               2^-120.20 absolute
//     the sums of W and of the result              2^-155.17 or less
//     the polynomial, a bound for each |r| <= R    2^-130.50
//     the tables and the reduction                 2^-161.17 or less
//     the sum, relative: eps_acc                   2^-120.19
//   A relative error e gives a distance of at most 2e between the mantissas,
//   so the slow path is at most 2^-119.19 from exp(x) in the mantissa, and
//   at most 2^-68.19 units of 2^-1074 for a subnormal result.
//
// Coverage of the correct round, from `fp_tables.py exp-coverage`
//   The round of the slow path is correct for x if no boundary of the round
//   is between exp(x) and the sum of the three doubles.  So each range of x
//   needs a bound of the distance of exp(x) to the nearest midpoint, larger
//   than the error of the path.  A list of hard cases bounds the distance of
//   each input off the list, and the subcommand measures each input on it.
//     |x| < 2^-54: exp(x) is between 1 + x and 1 + x + x^2, the nearest
//       midpoints are 1 - 2^-54 and 1 + 2^-53, so the result is 1, and
//       1 + x rounds to 1 too.  No search is necessary.
//     2^-54 <= |x| < 2^-30: an own exhaustive check.  exp increases with x, so
//       for each of the 12.6 million midpoints m in the range, the two doubles
//       next to ln(m) are the nearest to m.  The smallest distance is
//       2^-58.60 units in the last place at 0x1.9e9cbbfd6080bp-31, a relative
//       2^-110.60: a margin of 9.59 bits.  The list of CORE-MATH agrees with
//       the check in each binade from 2^-53, and it does not hold the 4096
//       inputs below 2^-53 and the 1792 inputs below -2^-54 that the check
//       finds.
//     2^-30 <= |x| with a result from 2^-1074 to 2^1024: Lefevre and Muller,
//       "Worst cases for correct rounding of the elementary functions in
//       double precision", ARITH 15 (2001), property 1: a mantissa distance
//       of at most 2^-112 gives the correct round in each of the four modes,
//       for each double x with |x| >= 2^-30.  The search ignores the
//       subnormal format, and each midpoint of the subnormal format is a
//       double or a midpoint of 53 bits, so the property holds there too.  A
//       margin of 7.19 bits.
//     The same range, second source: the list exp.wc of CORE-MATH
//       (BaCSeL, all modes): each input off the list is at least 2^-42 units
//       in the last place from a boundary, a relative 2^-95, and 2^-44 in
//       [-2^-11, -2^-12).  The hardest input on the list is
//       -0x1.12d31a20fb38bp+5, 2^-59.25 units, a relative 2^-111.69: a
//       margin of 8.50 bits.  For a subnormal result the list has the
//       threshold 2^-34 units of 2^-1074, and its hardest input is 2^-49.38
//       units at -0x1.6d9f7dd15f748p+9: a margin of 19.19 bits.
//     A result between 2^-1075 and 2^-1074: the only boundary is 2^-1075,
//       and the input next to ln(2^-1075), -0x1.74910d52d3051p+9, is 2^-44.20
//       units from it.  exp increases with x, so each other input is farther.
//     The edges of +inf and +0 come from ln(2^1024 - 2^970) and ln(2^-1075).
//
// Algorithm of exp(float)
//   The fast path computes in double with the same reduction in one fma and
//   a polynomial of degree 3 (error 2^-41.2).  When the double result is within
//   2^13 units of its last bit of a float midpoint, the round can be wrong, and
//   the slow path takes the input.  The slow path rounds exp(double) of the
//   input, the correctly rounded double.  So the round is wrong only when that
//   result is a float midpoint (fixy/fp/RoundToFloat.h), and the sweep of the
//   oracle lists each such input.  One unsigned compare of the
//   bits sends each |x| above 0x1.5d589ep+6, each infinity and each NaN to a
//   cold path: a NaN gives the canonical NaN, the edges give +inf and +0, and
//   each other input takes the slow path.  So a result below 2^-126 always
//   takes the slow path.  The fma sites of the fast path: one in the
//   reduction, two in the polynomial and one in the last step.
//   The exhaustive sweep of the 2^32 inputs against CORE-MATH proves the
//   correct round of each input (the target bench_fp_sweep).
//
// Tables
//   `python3 utils/tools/fp_tables.py exp` prints each table and constant of
//   this header with the pinned mpmath.

#include <foundation/Platform.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <fixy/fp/DoubleWord.h>
#include <fixy/fp/FmaOfFinite.h>
#include <fixy/fp/RoundToFloat.h>

#include <cstdint>

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "fixy/fp/Exp.h needs the floor crucible_fp_strict (-ffp-contract=off). Without it the compiler can fuse a multiply and an add, and the bits change with the level."
#endif

namespace fixy::fp::detail {

inline constexpr int kExpTableBits = 7;
inline constexpr std::uint64_t kExpTableMask = (std::uint64_t{1} << kExpTableBits) - 1;

// 128 / ln2, and -ln2/128 in a part of 36 bits and the rest.
inline constexpr double kExpInvLn2N = 0x1.71547652b82fep+7;
inline constexpr double kExpNegLn2HiN = -0x1.62e42fefa0000p-8;
inline constexpr double kExpNegLn2LoN = -0x1.cf79abc9e3b3ap-47;
// -ln2/128 as one double, for the float form.
inline constexpr double kExpNegLn2N = -0x1.62e42fefa39efp-8;
// The add of this value rounds a double of magnitude below 2^51 to an
// integer, and the low 32 bits of its result hold that integer.
inline constexpr double kExpShift = 0x1.8p52;

// ln2/128 in four parts for the slow path.  The first two parts have 35 bits.
inline constexpr double kExpLn2Part0 = 0x1.62e42fefc0000p-8;
inline constexpr double kExpLn2Part1 = -0x1.c610ca86c0000p-44;
inline constexpr double kExpLn2Part2 = -0x1.c4c67fc0d0951p-83;
inline constexpr double kExpLn2Part3 = 0x1.03cd0c99ca62ep-137;

// The minimax polynomial of exp(r) = 1 + r + r^2 * (C2 + C3 r + C4 r^2 + C5 r^3).
inline constexpr double kExpC2 = 0x1.ffffffffffdbdp-2;
inline constexpr double kExpC3 = 0x1.5555555555767p-3;
inline constexpr double kExpC4 = 0x1.55555cf144a31p-5;
inline constexpr double kExpC5 = 0x1.11110f0afd377p-7;
// The Taylor coefficients 1/n! of the slow path: a pair of doubles to n = 6,
// one double after it.
inline constexpr double kExpSlowC3High = 0x1.5555555555555p-3;
inline constexpr double kExpSlowC3Low = 0x1.5555555555555p-57;
inline constexpr double kExpSlowC4High = 0x1.5555555555555p-5;
inline constexpr double kExpSlowC4Low = 0x1.5555555555555p-59;
inline constexpr double kExpSlowC5High = 0x1.1111111111111p-7;
inline constexpr double kExpSlowC5Low = 0x1.1111111111111p-63;
inline constexpr double kExpSlowC6High = 0x1.6c16c16c16c17p-10;
inline constexpr double kExpSlowC6Low = -0x1.f49f49f49f49fp-65;
inline constexpr double kExpSlowC7 = 0x1.a01a01a01a01ap-13;
inline constexpr double kExpSlowC8 = 0x1.a01a01a01a01ap-16;
inline constexpr double kExpSlowC9 = 0x1.71de3a556c734p-19;
inline constexpr double kExpSlowC10 = 0x1.27e4fb7789f5cp-22;
inline constexpr double kExpSlowC11 = 0x1.ae64567f544e4p-26;

// The edges of the double form: exp(x) rounds to +inf above the first, to +0
// at or below the second, and is below 2^-1022 below the third.
inline constexpr double kExpLargestFinite = 0x1.62e42fefa39efp+9;
inline constexpr double kExpZeroAtOrBelow = -0x1.74910d52d3052p+9;
inline constexpr double kExpNormalFrom = -0x1.6232bdd7abcd2p+9;
// The margin of the rounding test of the fast path, relative to hi.
inline constexpr double kExpMargin = 0x1p-65;
// The polynomial of degree 3 of the fast path of the float form.
inline constexpr double kExpFloatC2 = 0x1.0000087d18c49p-1;
inline constexpr double kExpFloatC3 = 0x1.5555567d2e78ep-3;

// For each j of 0 to 127: the bits of tail_j, then the bits of s_j less
// j << 45.  s_j is the double nearest 2^(j/128), and tail_j is the double
// nearest (2^(j/128) - s_j) / s_j.  A read adds k << 45 to the second word,
// which puts the exponent of k/128 into the bits of s_j.  The generator
// prints the table in rows of four words.
// clang-format off
inline constexpr std::uint64_t kExpTable[2 * 128] = {
    0x0000000000000000ULL, 0x3FF0000000000000ULL, 0x3C9B3B4F1A88BF6EULL, 0x3FEFF63DA9FB3335ULL,
    0xBC7160139CD8DC5DULL, 0x3FEFEC9A3E778061ULL, 0xBC905E7A108766D1ULL, 0x3FEFE315E86E7F85ULL,
    0x3C8CD2523567F613ULL, 0x3FEFD9B0D3158574ULL, 0xBC8BCE8023F98EFAULL, 0x3FEFD06B29DDF6DEULL,
    0x3C60F74E61E6C861ULL, 0x3FEFC74518759BC8ULL, 0x3C90A3E45B33D399ULL, 0x3FEFBE3ECAC6F383ULL,
    0x3C979AA65D837B6DULL, 0x3FEFB5586CF9890FULL, 0x3C8EB51A92FDEFFCULL, 0x3FEFAC922B7247F7ULL,
    0x3C3EBE3D702F9CD1ULL, 0x3FEFA3EC32D3D1A2ULL, 0xBC6A033489906E0BULL, 0x3FEF9B66AFFED31BULL,
    0xBC9556522A2FBD0EULL, 0x3FEF9301D0125B51ULL, 0xBC5080EF8C4EEA55ULL, 0x3FEF8ABDC06C31CCULL,
    0xBC91C923B9D5F416ULL, 0x3FEF829AAEA92DE0ULL, 0x3C80D3E3E95C55AFULL, 0x3FEF7A98C8A58E51ULL,
    0xBC801B15EAA59348ULL, 0x3FEF72B83C7D517BULL, 0xBC8F1FF055DE323DULL, 0x3FEF6AF9388C8DEAULL,
    0x3C8B898C3F1353BFULL, 0x3FEF635BEB6FCB75ULL, 0xBC96D99C7611EB26ULL, 0x3FEF5BE084045CD4ULL,
    0x3C9AECF73E3A2F60ULL, 0x3FEF54873168B9AAULL, 0xBC8FE782CB86389DULL, 0x3FEF4D5022FCD91DULL,
    0x3C8A6F4144A6C38DULL, 0x3FEF463B88628CD6ULL, 0x3C807A05B0E4047DULL, 0x3FEF3F49917DDC96ULL,
    0x3C968EFDE3A8A894ULL, 0x3FEF387A6E756238ULL, 0x3C875E18F274487DULL, 0x3FEF31CE4FB2A63FULL,
    0x3C80472B981FE7F2ULL, 0x3FEF2B4565E27CDDULL, 0xBC96B87B3F71085EULL, 0x3FEF24DFE1F56381ULL,
    0x3C82F7E16D09AB31ULL, 0x3FEF1E9DF51FDEE1ULL, 0xBC3D219B1A6FBFFAULL, 0x3FEF187FD0DAD990ULL,
    0x3C8B3782720C0AB4ULL, 0x3FEF1285A6E4030BULL, 0x3C6E149289CECB8FULL, 0x3FEF0CAFA93E2F56ULL,
    0x3C834D754DB0ABB6ULL, 0x3FEF06FE0A31B715ULL, 0x3C864201E2AC744CULL, 0x3FEF0170FC4CD831ULL,
    0x3C8FDD395DD3F84AULL, 0x3FEEFC08B26416FFULL, 0xBC86A3803B8E5B04ULL, 0x3FEEF6C55F929FF1ULL,
    0xBC924AEDCC4B5068ULL, 0x3FEEF1A7373AA9CBULL, 0xBC9907F81B512D8EULL, 0x3FEEECAE6D05D866ULL,
    0xBC71D1E83E9436D2ULL, 0x3FEEE7DB34E59FF7ULL, 0xBC991919B3CE1B15ULL, 0x3FEEE32DC313A8E5ULL,
    0x3C859F48A72A4C6DULL, 0x3FEEDEA64C123422ULL, 0xBC9312607A28698AULL, 0x3FEEDA4504AC801CULL,
    0xBC58A78F4817895BULL, 0x3FEED60A21F72E2AULL, 0xBC7C2C9B67499A1BULL, 0x3FEED1F5D950A897ULL,
    0x3C4363ED60C2AC11ULL, 0x3FEECE086061892DULL, 0x3C9666093B0664EFULL, 0x3FEECA41ED1D0057ULL,
    0x3C6ECCE1DAA10379ULL, 0x3FEEC6A2B5C13CD0ULL, 0x3C93FF8E3F0F1230ULL, 0x3FEEC32AF0D7D3DEULL,
    0x3C7690CEBB7AAFB0ULL, 0x3FEEBFDAD5362A27ULL, 0x3C931DBDEB54E077ULL, 0x3FEEBCB299FDDD0DULL,
    0xBC8F94340071A38EULL, 0x3FEEB9B2769D2CA7ULL, 0xBC87DECCDC93A349ULL, 0x3FEEB6DAA2CF6642ULL,
    0xBC78DEC6BD0F385FULL, 0x3FEEB42B569D4F82ULL, 0xBC861246EC7B5CF6ULL, 0x3FEEB1A4CA5D920FULL,
    0x3C93350518FDD78EULL, 0x3FEEAF4736B527DAULL, 0x3C7B98B72F8A9B05ULL, 0x3FEEAD12D497C7FDULL,
    0x3C9063E1E21C5409ULL, 0x3FEEAB07DD485429ULL, 0x3C34C7855019C6EAULL, 0x3FEEA9268A5946B7ULL,
    0x3C9432E62B64C035ULL, 0x3FEEA76F15AD2148ULL, 0xBC8CE44A6199769FULL, 0x3FEEA5E1B976DC09ULL,
    0xBC8C33C53BEF4DA8ULL, 0x3FEEA47EB03A5585ULL, 0xBC845378892BE9AEULL, 0x3FEEA34634CCC320ULL,
    0xBC93CEDD78565858ULL, 0x3FEEA23882552225ULL, 0x3C5710AA807E1964ULL, 0x3FEEA155D44CA973ULL,
    0xBC93B3EFBF5E2228ULL, 0x3FEEA09E667F3BCDULL, 0xBC6A12AD8734B982ULL, 0x3FEEA012750BDABFULL,
    0xBC6367EFB86DA9EEULL, 0x3FEE9FB23C651A2FULL, 0xBC80DC3D54E08851ULL, 0x3FEE9F7DF9519484ULL,
    0xBC781F647E5A3ECFULL, 0x3FEE9F75E8EC5F74ULL, 0xBC86EE4AC08B7DB0ULL, 0x3FEE9F9A48A58174ULL,
    0xBC8619321E55E68AULL, 0x3FEE9FEB564267C9ULL, 0x3C909CCB5E09D4D3ULL, 0x3FEEA0694FDE5D3FULL,
    0xBC7B32DCB94DA51DULL, 0x3FEEA11473EB0187ULL, 0x3C94ECFD5467C06BULL, 0x3FEEA1ED0130C132ULL,
    0x3C65EBE1ABD66C55ULL, 0x3FEEA2F336CF4E62ULL, 0xBC88A1C52FB3CF42ULL, 0x3FEEA427543E1A12ULL,
    0xBC9369B6F13B3734ULL, 0x3FEEA589994CCE13ULL, 0xBC805E843A19FF1EULL, 0x3FEEA71A4623C7ADULL,
    0xBC94D450D872576EULL, 0x3FEEA8D99B4492EDULL, 0x3C90AD675B0E8A00ULL, 0x3FEEAAC7D98A6699ULL,
    0x3C8DB72FC1F0EAB4ULL, 0x3FEEACE5422AA0DBULL, 0xBC65B6609CC5E7FFULL, 0x3FEEAF3216B5448CULL,
    0x3C7BF68359F35F44ULL, 0x3FEEB1AE99157736ULL, 0xBC93091FA71E3D83ULL, 0x3FEEB45B0B91FFC6ULL,
    0xBC5DA9B88B6C1E29ULL, 0x3FEEB737B0CDC5E5ULL, 0xBC6C23F97C90B959ULL, 0x3FEEBA44CBC8520FULL,
    0xBC92434322F4F9AAULL, 0x3FEEBD829FDE4E50ULL, 0xBC85CA6CD7668E4BULL, 0x3FEEC0F170CA07BAULL,
    0x3C71AFFC2B91CE27ULL, 0x3FEEC49182A3F090ULL, 0x3C6DD235E10A73BBULL, 0x3FEEC86319E32323ULL,
    0xBC87C50422622263ULL, 0x3FEECC667B5DE565ULL, 0x3C8B1C86E3E231D5ULL, 0x3FEED09BEC4A2D33ULL,
    0xBC91BBD1D3BCBB15ULL, 0x3FEED503B23E255DULL, 0x3C90CC319CEE31D2ULL, 0x3FEED99E1330B358ULL,
    0x3C8469846E735AB3ULL, 0x3FEEDE6B5579FDBFULL, 0xBC82DFCD978E9DB4ULL, 0x3FEEE36BBFD3F37AULL,
    0x3C8C1A7792CB3387ULL, 0x3FEEE89F995AD3ADULL, 0xBC907B8F4AD1D9FAULL, 0x3FEEEE07298DB666ULL,
    0xBC55C3D956DCAEBAULL, 0x3FEEF3A2B84F15FBULL, 0xBC90A40E3DA6F640ULL, 0x3FEEF9728DE5593AULL,
    0xBC68D6F438AD9334ULL, 0x3FEEFF76F2FB5E47ULL, 0xBC91EEE26B588A35ULL, 0x3FEF05B030A1064AULL,
    0x3C74FFD70A5FDDCDULL, 0x3FEF0C1E904BC1D2ULL, 0xBC91BDFBFA9298ACULL, 0x3FEF12C25BD71E09ULL,
    0x3C736EAE30AF0CB3ULL, 0x3FEF199BDD85529CULL, 0x3C8EE3325C9FFD94ULL, 0x3FEF20AB5FFFD07AULL,
    0x3C84E08FD10959ACULL, 0x3FEF27F12E57D14BULL, 0x3C63CDAF384E1A67ULL, 0x3FEF2F6D9406E7B5ULL,
    0x3C676B2C6C921968ULL, 0x3FEF3720DCEF9069ULL, 0xBC808A1883CCB5D2ULL, 0x3FEF3F0B555DC3FAULL,
    0xBC8FAD5D3FFFFA6FULL, 0x3FEF472D4A07897CULL, 0xBC900DAE3875A949ULL, 0x3FEF4F87080D89F2ULL,
    0x3C74A385A63D07A7ULL, 0x3FEF5818DCFBA487ULL, 0xBC82919E2040220FULL, 0x3FEF60E316C98398ULL,
    0x3C8E5A50D5C192ACULL, 0x3FEF69E603DB3285ULL, 0x3C843A59AC016B4BULL, 0x3FEF7321F301B460ULL,
    0xBC82D52107B43E1FULL, 0x3FEF7C97337B9B5FULL, 0xBC892AB93B470DC9ULL, 0x3FEF864614F5A129ULL,
    0x3C74B604603A88D3ULL, 0x3FEF902EE78B3FF6ULL, 0x3C83C5EC519D7271ULL, 0x3FEF9A51FBC74C83ULL,
    0xBC8FF7128FD391F0ULL, 0x3FEFA4AFA2A490DAULL, 0xBC8DAE98E223747DULL, 0x3FEFAF482D8E67F1ULL,
    0x3C8EC3BC41AA2008ULL, 0x3FEFBA1BEE615A27ULL, 0x3C842B94C3A9EB32ULL, 0x3FEFC52B376BBA97ULL,
    0x3C8A64A931D185EEULL, 0x3FEFD0765B6E4540ULL, 0xBC8E37BAE43BE3EDULL, 0x3FEFDBFDAD9CBE14ULL,
    0x3C77893B4D91CD9DULL, 0x3FEFE7C1819E90D8ULL, 0x3C5305C14160CC89ULL, 0x3FEFF3C22B8F71F1ULL,
};

// For each j of 0 to 127, the bits of low_j, the double nearest
// tau_j - tail_j, where tau_j = (2^(j/128) - s_j) / s_j exactly.  So
// |tau_j - tail_j - low_j| <= 2^-161.18.  Only the slow path reads it.
inline constexpr std::uint64_t kExpTailLow[128] = {
    0x0000000000000000ULL, 0x3928A7A22972BA14ULL, 0x391F6015898E4B03ULL, 0x3912BE8EE4A92D46ULL,
    0x39244C922A9B2B23ULL, 0xB909F8ED95DFFFC4ULL, 0x390B9B11AD82685FULL, 0xB9369DB4B68C9A5EULL,
    0xB93594DFEEF9B094ULL, 0xB908C623DD8DF7D1ULL, 0x38BDD33EE7C61A9FULL, 0xB906227050942779ULL,
    0x39378A37793BD990ULL, 0x38F1D779FC7A1B5BULL, 0x392EAB1EBB431B52ULL, 0xB9204C46906DE1D2ULL,
    0x3927CA91BA6AB110ULL, 0x39250E6BAC42D66EULL, 0x3916B479BCC6155DULL, 0xB9375B3F3662487DULL,
    0xB925A6C4849FFA0AULL, 0xB923C59FA185A630ULL, 0xB923375C863A5887ULL, 0x39156342D8A461EEULL,
    0xB9273CE42E5ADDF9ULL, 0x39248346F20E387FULL, 0xB920AC00CA787E82ULL, 0x393F5534006E9372ULL,
    0x39092A971C02FAE6ULL, 0xB8DF2E946F6FCA50ULL, 0xB92C7B41C67538E0ULL, 0xB8FD31CD36BC8363ULL,
    0xB8F17ADBE862E719ULL, 0x392FB553BC0BD3C7ULL, 0x38EDAE4E9501C824ULL, 0xB9106E77DBE52DEBULL,
    0xB9329A3D89B24B82ULL, 0xB901F89E2E4F5B79ULL, 0xB8FA967549FBEA82ULL, 0x3925411DB4CBE19BULL,
    0xB9278E17B6B693C4ULL, 0x38D3E0285B53F920ULL, 0x38EEAFA2E5163902ULL, 0xB912A218D08C9A72ULL,
    0x38EFD123547EAC09ULL, 0x391A4BA941299385ULL, 0xB9060CF17A743180ULL, 0x392F7CAB07500CDEULL,
    0x3919C294119C9A68ULL, 0xB91839D9478815D7ULL, 0xB91B1D7B433942B5ULL, 0xB9286B714E6F971AULL,
    0xB91D759E0525A7D9ULL, 0x392629C71C8AB284ULL, 0xB924DC0D0A5A4B39ULL, 0x38EAA4BD416621E1ULL,
    0x393680601784AB98ULL, 0xB8C0A7377E6B7108ULL, 0x393A0E5DE64BEC26ULL, 0xB8FBDDB44B66B447ULL,
    0x38F018F24491A23AULL, 0xB920383ABB54A836ULL, 0x392E3C5ED6541AFFULL, 0xB8FA8A0B5ADE8A2CULL,
    0xB93B9B1E8AAD39E1ULL, 0x3901453EA30680A3ULL, 0xB909CBF3CCFB7000ULL, 0xB91E8D954E6A625BULL,
    0x3908136D5AC775B9ULL, 0x38F5A2192235A2F9ULL, 0xB91B801749F1D00AULL, 0xB938471DE9D202ECULL,
    0x3918D9B80F69CB44ULL, 0x393688850663DEAEULL, 0xB90609145D671D93ULL, 0x392D02879520C4F3ULL,
    0x3938E0F32DAEC100ULL, 0xB8E30B605606AEB9ULL, 0x391820E7FFB04DA4ULL, 0x3901A30D577B03B1ULL,
    0x392D2B74A6EAD83DULL, 0xB8FAE023DCDD75C8ULL, 0x38DAD5EFD6C431A0ULL, 0x3934C7F97D918464ULL,
    0x38F2C5BB4EF23D71ULL, 0x3900A5A13D8DE6CFULL, 0x38FCB34A84321CA0ULL, 0x3925A88AF1867412ULL,
    0x391D2B69CD832D47ULL, 0xB8CE45C28BFA24C4ULL, 0x392E14263C54CB83ULL, 0xB8D43B0E44DC78FCULL,
    0x392EE48CD2B75F73ULL, 0x3931DE03750EA1CCULL, 0xB8D8F6A981A59CFCULL, 0xB92238F080694D21ULL,
    0xB9111391BF5631FEULL, 0x3937D08ED677681FULL, 0x38E8C250A062B600ULL, 0x39288FD5911A5304ULL,
    0x38F3CF21DC6C9A48ULL, 0xB93180C598014BB6ULL, 0x38EBD0D1EB17359AULL, 0xB93B01A8E1AD9B15ULL,
    0x390DFDF3962EAF5BULL, 0xB9247E1E12FC579AULL, 0x390FAE3DA7F85D20ULL, 0xB8D2DA68B71D5321ULL,
    0xB9033448A23A8C15ULL, 0x392025BB1FA1B33AULL, 0x3920714F32819D5FULL, 0x391151E6A7FB3018ULL,
    0x391CE5D0A1C6B509ULL, 0x392A284A92E86855ULL, 0x38FF6243AFBC590DULL, 0xB91D3ABD36D3B354ULL,
    0xB921F8F5E10BD823ULL, 0x391F8671B6297C6BULL, 0x38E75370E9EBDF79ULL, 0x38F6D3102F96455FULL,
    0xB92E6B4F781F7888ULL, 0x39190345DEC48C57ULL, 0x3916A17C820F20C7ULL, 0x3929D7AEAD50488DULL,
    0xB919997FDEFCBA87ULL, 0x39239204DC998B01ULL, 0xB918647AD17DA270ULL, 0x38C0EB807D62D95CULL,
};
// clang-format on

// The float form: its inputs past the edges.  exp(x) rounds to +inf above
// the first edge, to +0 below the second, and is below 2^-126 below the
// third.
inline constexpr float kExpFloatOverflowAbove = 0x1.62e42ep+6f;
inline constexpr float kExpFloatZeroBelow = -0x1.9fe368p+6f;
inline constexpr float kExpFloatSubnormalBelow = -0x1.5d589ep+6f;
// The bits of |kExpFloatSubnormalBelow| without the sign, shifted left by one.
// Each x with |x| at or below that magnitude takes the fast path: its result
// is a normal finite float.
inline constexpr std::uint32_t kExpFloatFastBitsTwice = __builtin_bit_cast(std::uint32_t, -kExpFloatSubnormalBelow)
                                                     << 1;
// The tolerance of the rounding test of the float fast path, in units of the
// last bit of the double result: the error 2^-41 of the fast path times 2^53,
// with a margin of two.
inline constexpr std::uint64_t kExpFloatTolerance = std::uint64_t{1} << 13;

inline namespace CRUCIBLE_SIMD_TARGET {

// The top 12 bits of a double: the sign and the biased exponent.
[[nodiscard]] constexpr std::uint32_t exp_top12_(double value) noexcept {
    return static_cast<std::uint32_t>(__builtin_bit_cast(std::uint64_t, value) >> 52);
}

// hi + lo of the fast path, with the bits of s less scale_shift.  The fast
// path takes a shift of 0, and the edge path scales s by 2^-1009 or 2^1022.
// The error of hi + lo is the budget eps_fast of the comment of the header,
// for each x with |k| <= 2^17 and a normal scaled s.
[[nodiscard, gnu::always_inline]] constexpr DoubleWord exp_pair_(double value, std::uint64_t scale_shift) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    // r1 = x - k L1 is exact, so the multiply and the add of the levels with
    // no fused multiply-add give the bits of the fma.
#if defined(__FMA__) || defined(__aarch64__)
    double rounded = fma_of_finite(value, kExpInvLn2N, kExpShift);
    std::uint64_t const shifted_bits = __builtin_bit_cast(std::uint64_t, rounded);
    rounded -= kExpShift;
    double const reduced_first = fma_of_finite(rounded, kExpNegLn2HiN, value);
#else
    double rounded = kExpInvLn2N * value + kExpShift;
    std::uint64_t const shifted_bits = __builtin_bit_cast(std::uint64_t, rounded);
    rounded -= kExpShift;
    double const reduced_first = value + rounded * kExpNegLn2HiN;
#endif
    double const reduced = fma_of_finite(rounded, kExpNegLn2LoN, reduced_first);
    double const reduced_low = fma_of_finite(rounded, kExpNegLn2LoN, reduced_first - reduced);
    std::uint64_t const index = 2 * (shifted_bits & kExpTableMask);
    double const tail = __builtin_bit_cast(double, kExpTable[index]);
    double const scale =
        __builtin_bit_cast(double, kExpTable[index + 1] + (shifted_bits << (52 - kExpTableBits)) - scale_shift);
    double const squared = reduced * reduced;
    double const low_pair = fma_of_finite(reduced, kExpC3, kExpC2);
    double const high_pair = fma_of_finite(reduced, kExpC5, kExpC4);
    // r_lo (1 + r_hi) joins after the polynomial, so r_lo is off the longest
    // chain.
    double const tail_terms = fma_of_finite(tail, reduced, tail);
    double const low_terms = fma_of_finite(reduced_low, reduced, reduced_low);
    double const polynomial = fma_of_finite(squared, high_pair, low_pair);
    double const rest = fma_of_finite(squared, polynomial, tail_terms) + low_terms;
    double const high = fma_of_finite(scale, reduced, scale);
    // s - hi is exact: hi is within 2^-8.4 of s.
    double const high_error = fma_of_finite(scale, reduced, scale - high);
    return DoubleWord{high, fma_of_finite(scale, rest, high_error)};
}

// The slow path: exp(x) correctly rounded for 2^-54 <= |x| and each x from
// above kExpZeroAtOrBelow to kExpLargestFinite, with a subnormal result too.
// It computes y = exp(x) / 2^(k >> 7) in three doubles with the relative
// error eps_acc of the comment of the header, and rounds their sum.  The
// coverage table of that comment shows that no boundary of the round is that
// close to exp(x).
[[gnu::cold, gnu::noinline, nodiscard]] constexpr double exp_accurate_(double value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    // k from a multiply and an add on each level, |k| <= 137602.
    double rounded = kExpInvLn2N * value + kExpShift;
    std::uint64_t const shifted_bits = __builtin_bit_cast(std::uint64_t, rounded);
    rounded -= kExpShift;
    auto const count = static_cast<std::int32_t>(static_cast<std::uint32_t>(shifted_bits));
    std::int32_t const power = count >> kExpTableBits;
    auto const entry = static_cast<std::uint64_t>(count) & kExpTableMask;
    // r = x - k ln2/128 in three doubles.  k P0 and k P1 are exact, so is
    // x - k P0, and each sum below is exact but the last.
    double const reduced_first = value - rounded * kExpLn2Part0;
    DoubleWord const reduced_pair = two_sum_(reduced_first, -(rounded * kExpLn2Part1));
    DoubleWord const product2 = two_product_(rounded, kExpLn2Part2);
    double const product3 = rounded * kExpLn2Part3;
    DoubleWord const middle = two_sum_(reduced_pair.low, -product2.high);
    double const last = (middle.low - product2.low) - product3;
    DoubleWord const head = two_sum_(reduced_pair.high, middle.high);
    DoubleWord const tail = two_sum_(head.low, last);
    DoubleWord const reduced = fast_two_sum_(head.high, tail.high);
    double const reduced_last = tail.low;
    // Q = 1/2 + r/6 + ... + r^9/11!: the part of degree 7 to 11 in doubles on
    // the high part of r, then a Horner sum in pairs of doubles.
    double horner = fma_of_finite(reduced.high, kExpSlowC11, kExpSlowC10);
    horner = fma_of_finite(reduced.high, horner, kExpSlowC9);
    horner = fma_of_finite(reduced.high, horner, kExpSlowC8);
    horner = fma_of_finite(reduced.high, horner, kExpSlowC7);
    DoubleWord sum = dw_plus_dw_(DoubleWord{kExpSlowC6High, kExpSlowC6Low}, dw_times_fp_(reduced, horner));
    sum = dw_plus_dw_(DoubleWord{kExpSlowC5High, kExpSlowC5Low}, dw_times_dw_(reduced, sum));
    sum = dw_plus_dw_(DoubleWord{kExpSlowC4High, kExpSlowC4Low}, dw_times_dw_(reduced, sum));
    sum = dw_plus_dw_(DoubleWord{kExpSlowC3High, kExpSlowC3Low}, dw_times_dw_(reduced, sum));
    DoubleWord const quotient = dw_plus_fp_(dw_times_dw_(reduced, sum), 0.5);
    DoubleWord const curve = dw_times_dw_(dw_times_dw_(reduced, reduced), quotient);
    // V = r + r^2 Q = v1 + v3 + v4.
    DoubleWord const v1 = two_sum_(reduced.high, curve.high);
    DoubleWord const v2 = two_sum_(reduced.low, curve.low);
    DoubleWord const v3 = two_sum_(v1.low, v2.high);
    double const v4 = (v2.low + v3.low) + reduced_last;
    // W = tau + V + tau V = w1 + w3 + w_rest, with tau = tail + low.
    double const tail_high = __builtin_bit_cast(double, kExpTable[2 * entry]);
    double const tail_low = __builtin_bit_cast(double, kExpTailLow[entry]);
    DoubleWord const cross = two_product_(tail_high, v1.high);
    double const cross_rest = fma_of_finite(tail_high, v3.high, tail_low * v1.high);
    DoubleWord const w1 = two_sum_(v1.high, tail_high);
    DoubleWord const w2 = two_sum_(w1.low, cross.high);
    DoubleWord const w3 = two_sum_(w2.high, v3.high);
    double const w_rest = ((((w2.low + w3.low) + cross.low) + tail_low) + v4) + cross_rest;
    // y = s (1 + W) = z1 + z3 + z_rest, with s = 2^(j/128) rounded, in [1, 2).
    double const scale = __builtin_bit_cast(double, kExpTable[2 * entry + 1] + (entry << (52 - kExpTableBits)));
    DoubleWord const p1 = two_product_(scale, w1.high);
    DoubleWord const p3 = two_product_(scale, w3.high);
    DoubleWord const z1 = fast_two_sum_(scale, p1.high);
    DoubleWord const z2 = two_sum_(z1.low, p1.low);
    DoubleWord const z3 = two_sum_(z2.high, p3.high);
    double const z_rest = ((z2.low + z3.low) + p3.low) + scale * w_rest;
    // A subnormal result adds G = 2^(-1022 - (k >> 7)): G + y is in [G, 2G),
    // where the step of the doubles is the step of the subnormal result.
    bool const is_subnormal = value < kExpNormalFrom;
    double const offset = is_subnormal ? __builtin_bit_cast(double, static_cast<std::uint64_t>(1 - power) << 52) : 0.0;
    DoubleWord const top = two_sum_(offset, z1.high);
    DoubleWord const second = two_sum_(top.low, z3.high);
    DoubleWord const head_pair = fast_two_sum_(top.high, second.high);
    DoubleWord const rest_pair = two_sum_(head_pair.low, second.low + z_rest);
    double const result = round_triple_(head_pair.high, rest_pair.high, rest_pair.low);
    if (!is_subnormal) {
        // The result is normal, so the add of k >> 7 to its exponent is exact.
        return __builtin_bit_cast(double, __builtin_bit_cast(std::uint64_t, result)
                                              + (static_cast<std::uint64_t>(power) << 52));
    }
    // result - G is exact, and each multiply is exact.
    double const fraction =
        (result - offset) * __builtin_bit_cast(double, static_cast<std::uint64_t>(2045 + power) << 52);
    return fraction * 0x1p-1022;
}

// The edge path: |x| >= 512, an infinity and a NaN.
[[gnu::cold, gnu::noinline, nodiscard]] constexpr double exp_edge_(double value) noexcept {
    if ((__builtin_bit_cast(std::uint64_t, value) << 1) > 0xFFE0000000000000ULL)
        return ::foundation::core::quiet_nan_of<double>;
    if (value > kExpLargestFinite) return ::foundation::core::infinity_of<double>;
    if (value <= kExpZeroAtOrBelow) return 0.0;
    if (value < kExpNormalFrom) return exp_accurate_(value);
    bool const is_positive = value > 0.0;
    std::uint64_t const shift =
        is_positive ? std::uint64_t{1009} << 52 : std::uint64_t{0} - (std::uint64_t{1022} << 52);
    DoubleWord const pair = exp_pair_(value, shift);
    double const margin = pair.high * kExpMargin;
    double const upper = pair.high + (pair.low + margin);
    double const lower = pair.high + (pair.low - margin);
    if (__builtin_islessgreater(upper, lower)) return exp_accurate_(value);
    return is_positive ? upper * 0x1p1009 : upper * 0x1p-1022;
}

// The midpoint inputs of the float form: each input whose exp(double) is a
// float midpoint, with its correctly rounded result.  The sweep of the oracle
// gives the list, and a midpoint input that is not in it takes the round to
// even.
[[gnu::cold, nodiscard]] constexpr float exp_float_midpoint_(std::uint32_t input_bits, double result) noexcept {
    switch (input_bits) {
        default:
            return static_cast<float>(result);
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr double exp(double value) noexcept {
    std::uint32_t const top = detail::exp_top12_(value) & 0x7ffu;
    std::uint32_t constexpr tiny_top = 0x3c9u;  // the top of 2^-54
    std::uint32_t constexpr large_top = 0x408u;  // the top of 512
    if (top - tiny_top >= large_top - tiny_top) [[unlikely]] {
        // |x| < 2^-54: exp(x) rounds to 1, and 1 + x gives that round.
        if (top < tiny_top) return 1.0 + value;
        return detail::exp_edge_(value);
    }
    detail::DoubleWord const pair = detail::exp_pair_(value, 0);
    double const margin = pair.high * detail::kExpMargin;
    double const upper = pair.high + (pair.low + margin);
    double const lower = pair.high + (pair.low - margin);
    if (__builtin_islessgreater(upper, lower)) [[unlikely]]
        return detail::exp_accurate_(value);
    return upper;
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp

namespace fixy::fp::detail {

inline namespace CRUCIBLE_SIMD_TARGET {

// The slow path of the float form: the round of exp(double), with the list of
// the midpoint inputs.
[[gnu::cold, gnu::noinline, nodiscard]] constexpr float exp_float_slow_(float value) noexcept {
    double const result = ::fixy::fp::exp(static_cast<double>(value));
    if (is_float_midpoint_(result)) [[unlikely]] {
        return exp_float_midpoint_(__builtin_bit_cast(std::uint32_t, value), result);
    }
    return static_cast<float>(result);
}

// The inputs past the range of the fast path of the float form: a NaN gives
// the canonical NaN, a result past the largest float gives +inf, a result
// below the smallest subnormal gives +0, and each other input takes the slow
// path.  The slow path holds the subnormal results and each x above
// |kExpFloatSubnormalBelow| below the edge of +inf.
[[gnu::cold, gnu::noinline, nodiscard]] constexpr float exp_float_edge_(float value) noexcept {
    if ((__builtin_bit_cast(std::uint32_t, value) & 0x7fffffffu) > 0x7f800000u)
        return ::foundation::core::quiet_nan_of<float>;
    if (value > kExpFloatOverflowAbove) return ::foundation::core::infinity_of<float>;
    if (value < kExpFloatZeroBelow) return 0.0f;
    return exp_float_slow_(value);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr float exp(float value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    // One unsigned compare of the bits without the sign holds each input past
    // the range of the fast path: the NaNs, the infinities, and each |x| above
    // the edge of a subnormal result.  The cold path sorts them.
    if ((__builtin_bit_cast(std::uint32_t, value) << 1) > detail::kExpFloatFastBitsTwice) [[unlikely]]
        return detail::exp_float_edge_(value);
    double const input = static_cast<double>(value);
    // With a fused multiply-add, one fma gives x N / ln2 + the shift with one
    // round, two cycles before a multiply and an add.  So k can differ from
    // the k of a multiply and an add near a half.  Each such k gives a reduced
    // r inside the range of the polynomial, and the rounding test below keeps
    // the result correctly rounded, so the float bits are the same on each
    // level.  At x86-64 and x86-64-v2 the fma is a form in doubles of about
    // 100 instructions, so the multiply and the add stay there.
#if defined(__FMA__) || defined(__aarch64__)
    double rounded = fma_of_finite(input, detail::kExpInvLn2N, detail::kExpShift);
#else
    double rounded = detail::kExpInvLn2N * input + detail::kExpShift;
#endif
    std::uint64_t const shifted_bits = __builtin_bit_cast(std::uint64_t, rounded);
    rounded -= detail::kExpShift;
    double const reduced = fma_of_finite(rounded, detail::kExpNegLn2N, input);
    std::uint64_t const index = 2 * (shifted_bits & detail::kExpTableMask);
    double const scale =
        __builtin_bit_cast(double, detail::kExpTable[index + 1] + (shifted_bits << (52 - detail::kExpTableBits)));
    double const pair = fma_of_finite(reduced, detail::kExpFloatC3, detail::kExpFloatC2);
    double const sum = fma_of_finite(reduced * reduced, pair, reduced);
    double const result = fma_of_finite(scale, sum, scale);
    if (detail::is_near_float_midpoint_(result, detail::kExpFloatTolerance)) [[unlikely]] {
        return detail::exp_float_slow_(value);
    }
    return static_cast<float>(result);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp
