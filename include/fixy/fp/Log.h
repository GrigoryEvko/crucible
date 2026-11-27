#pragma once

// log of a double and of a float, with the same bits on each target and each
// level of the instruction set.
//
//   fixy::fp::log(double)   error of at most 0.53 units in the last place
//   fixy::fp::log(float)    the correctly rounded result for each input
//
// Each function is total.  ±0 gives -inf, +inf gives +inf, 1 gives +0, and a
// negative value (-inf and each negative subnormal value too) gives the
// canonical quiet NaN quiet_nan_of<T>, as does a NaN.  A subnormal input goes
// through a normalization with integer operations, so a mode that reads a
// subnormal input as zero does not change the result.  No function reads or
// writes errno or a flag of the floating-point unit on purpose.
//
// Mode of the register
//   Each function assumes the default mode of the floating-point unit: the
//   round to nearest with ties to even, no flush of a subnormal result to
//   zero, and no read of a subnormal input as zero.  Another mode changes the
//   bits.  PyTorch can set the flush of subnormal values in the vessel
//   process.  A kernel scope proves the mode through its FpScope token.
//
// Operations
//   As in fixy/fp/Exp.h: IEEE 754 add, subtract and multiply, explicit fma of
//   detail::fma_of_finite, integer operations on the bits and table reads, in
//   the order of
//   the source, under the floor crucible_fp_strict (-ffp-contract=off).  The
//   functions are in the inline namespace CRUCIBLE_SIMD_TARGET.
//
// Algorithm of log(double): the table method of ARM optimized-routines.
//   x = 2^k z with z in [0x1.6p-1, 0x1.6p0).  The 7 bits of z below its
//   exponent pick an interval i of 128.  invc_i is the double nearest 1/c for
//   the center c of the interval, and logc_i = -log(invc_i) exactly, in a high
//   part (a multiple of 2^-43) and a low part.
//   r = fma(z, invc_i, -1) rounds one time, and |r| < 2^-8.
//   w = k * ln2_hi + logc_hi is exact: two multiples of 2^-43 below 2^10.
//   log(x) = (w + r) + (the rest of w + r) + k ln2_lo + logc_lo
//            + r^2 (A0 + r A1 + r^2 A2 + r^3 A3 + r^4 A4).
//   The polynomial is the minimax polynomial of log1p(r) - r for the absolute
//   error on the interval of r, with an error of 2^-64.1.
//   Near 1, for x in [1 - 2^-4, 1 + 0x1.09p-4], r = x - 1 is exact, and
//   log(x) = r - r^2/2 + r^3 (B1 + r B2 + ... + r^9 B10).  The term r^2/2 is
//   in two parts through one fma, and the polynomial is the minimax
//   polynomial for the relative error, with an error of 2^-61.9.
//   The fma sites: eight in the table path and eleven near 1.
//   The error estimate of the method is 0.5 + 2.06/128 for the round of r and
//   2^-8 for the polynomial, about 0.52 units, as ARM states for its table
//   path.  The sampled sweep gives 0.518 near 1.12, so the bound is 0.53.
//
// Algorithm of log(float)
//   The fast path computes in double.  x = 2^k z with z in [0x1.66p-1,
//   0x1.66p0) and 64 intervals.  invc_i has 29 significant bits, and the
//   interval that holds 1 has invc = 1 and logc = 0.  z has 24 bits, so
//   z * invc_i is exact in double, and r = z * invc_i - 1 is exact.
//   log(x) = fma(k, ln2, logc_i) + (r + r^2 P(r)), P of degree 4, with a
//   relative error of the polynomial of 2^-49.5.  The rounding test and the
//   slow path are those of exp(float) in fixy/fp/Exp.h, with log(double) in the
//   slow path.  The fma sites of the fast path: one for k ln2, four in the
//   polynomial, one for r + r^2 P(r).
//   The exhaustive sweep of the 2^32 inputs against CORE-MATH proves the
//   correct round of each input (the target bench_fp_sweep).
//
// Tables
//   `python3 utils/tools/fp_tables.py log` prints the table and the constants
//   of log(double), and `python3 utils/tools/fp_tables.py logf` those of
//   log(float), with the pinned mpmath.

#include <foundation/Platform.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <fixy/fp/FmaOfFinite.h>
#include <fixy/fp/RoundToFloat.h>

#include <cstdint>

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "fixy/fp/Log.h needs the floor crucible_fp_strict (-ffp-contract=off). Without it the compiler can fuse a multiply and an add, and the bits change with the level."
#endif

namespace fixy::fp::detail {

inline constexpr int kLogTableBits = 7;
// The bits of 0x1.6p-1: the start of the range of z.
inline constexpr std::uint64_t kLogOffset = 0x3FE6000000000000ULL;
// The bits of 1 - 2^-4 and 1 + 0x1.09p-4: the edges of the path near 1.
inline constexpr std::uint64_t kLogNearLow = 0x3FEE000000000000ULL;
inline constexpr std::uint64_t kLogNearHigh = 0x3FF1090000000000ULL;
// ln2 in a part that is a multiple of 2^-42, and the rest.
inline constexpr double kLogLn2Hi = 0x1.62e42fefa3800p-1;
inline constexpr double kLogLn2Lo = 0x1.ef35793c76730p-45;

// The minimax polynomial of log1p(r) - r = r^2 (A0 + A1 r + ... + A4 r^4).
inline constexpr double kLogA0 = -0x1.0000000000001p-1;
inline constexpr double kLogA1 = 0x1.555555550b2ecp-2;
inline constexpr double kLogA2 = -0x1.fffffffebc5f1p-3;
inline constexpr double kLogA3 = 0x1.999b49c6b6945p-3;
inline constexpr double kLogA4 = -0x1.55576176149b9p-3;

// The minimax polynomial of (log1p(r) - r + r^2/2) / r^3 near 1.
inline constexpr double kLogB1 = 0x1.5555555555577p-2;
inline constexpr double kLogB2 = -0x1.ffffffffffdb3p-3;
inline constexpr double kLogB3 = 0x1.999999995deb1p-3;
inline constexpr double kLogB4 = -0x1.555555567c833p-3;
inline constexpr double kLogB5 = 0x1.24924a342f564p-3;
inline constexpr double kLogB6 = -0x1.fffffa24b2b71p-4;
inline constexpr double kLogB7 = 0x1.c7184291f908ep-4;
inline constexpr double kLogB8 = -0x1.999ece3bb4caep-4;
inline constexpr double kLogB9 = 0x1.7818362d0c51ap-4;
inline constexpr double kLogB10 = -0x1.551964dbb81cep-4;

// One interval of z: the double nearest 1/c, and -log of it in two parts.
struct LogEntry {
    double invc = 0.0;
    double logc_high = 0.0;
    double logc_low = 0.0;
};

// clang-format off
inline constexpr LogEntry kLogTable[128] = {
    {0x1.734f0c541fe8dp+0, -0x1.7cc7f7db46800p-2, -0x1.073c78ffb8648p-45},
    {0x1.713786d9c7c09p+0, -0x1.76feecb947000p-2, -0x1.75b19c9852c57p-46},
    {0x1.6f26016f26017p+0, -0x1.713e33a46a000p-2, -0x1.7bc12617e9472p-46},
    {0x1.6d1a62681c861p+0, -0x1.6b85b4cffa000p-2, -0x1.fe6e50d372503p-45},
    {0x1.6b1490aa31a3dp+0, -0x1.65d558d4ce000p-2, -0x1.558fd2dc5bdc0p-51},
    {0x1.691473a88d0c0p+0, -0x1.602d08af09000p-2, -0x1.ec69176df3f65p-46},
    {0x1.6719f3601671ap+0, -0x1.5a8cadbbee000p-2, 0x1.7be9b0af7ecf8p-48},
    {0x1.6524f853b4aa3p+0, -0x1.54f431b7be000p-2, -0x1.a7ef4c0910952p-46},
    {0x1.63356b88ac0dep+0, -0x1.4f637ebba9800p-2, -0x1.f4b9a676da36ep-51},
    {0x1.614b36831ae94p+0, -0x1.49da7f3bcc800p-2, 0x1.f01d964a168cdp-45},
    {0x1.5f66434292dfcp+0, -0x1.44591e053a000p-2, 0x1.6de5892923d88p-47},
    {0x1.5d867c3ece2a5p+0, -0x1.3edf463c16800p-2, -0x1.ee37ad01a7821p-49},
    {0x1.5babcc647fa91p+0, -0x1.396ce359bc000p-2, 0x1.5a15c5663663dp-47},
    {0x1.59d61f123ccaap+0, -0x1.3401e12aec800p-2, -0x1.d01f95523adc6p-45},
    {0x1.5805601580560p+0, -0x1.2e9e2bce12000p-2, -0x1.42e0c128d1dc2p-45},
    {0x1.56397ba7c52e2p+0, -0x1.2941afb186800p-2, -0x1.be05a919e3aebp-45},
    {0x1.54725e6bb82fep+0, -0x1.23ec5991eb800p-2, -0x1.248176eba35bcp-45},
    {0x1.52aff56a8054bp+0, -0x1.1e9e167889800p-2, -0x1.f5164b0dd2688p-46},
    {0x1.50f22e111c4c5p+0, -0x1.1956d3b9bc000p-2, -0x1.7c8873ad1aa14p-45},
    {0x1.4f38f62dd4c9bp+0, -0x1.14167ef367800p-2, 0x1.ee107db2550acp-48},
    {0x1.4d843bedc2c4cp+0, -0x1.0edd060b78000p-2, -0x1.044b52d8435f5p-47},
    {0x1.4bd3edda68fe1p+0, -0x1.09aa572e6c800p-2, 0x1.2b8187a32f2f6p-46},
    {0x1.4a27fad76014ap+0, -0x1.047e60cde8000p-2, -0x1.dba110d397f3cp-45},
    {0x1.4880522014880p+0, -0x1.feb2233ea0000p-3, -0x1.f2c18de00938bp-45},
    {0x1.46dce34596066p+0, -0x1.f474b134df000p-3, -0x1.13f9838821289p-46},
    {0x1.453d9e2c776cap+0, -0x1.ea4449f04b000p-3, 0x1.42df33919ab94p-45},
    {0x1.43a2730abee4dp+0, -0x1.e020cc6236000p-3, 0x1.52df0adb91424p-45},
    {0x1.420b5265e5951p+0, -0x1.d60a17f903000p-3, -0x1.44eaf207be58ep-45},
    {0x1.40782d10e6566p+0, -0x1.cc000c9db4000p-3, 0x1.d6e985d57aff9p-46},
    {0x1.3ee8f42a5af07p+0, -0x1.c2028ab180000p-3, 0x1.92a3ee55c7ac6p-45},
    {0x1.3d5d991aa75c6p+0, -0x1.b811730b82000p-3, -0x1.e9e283b9cd768p-46},
    {0x1.3bd60d9232955p+0, -0x1.ae2ca6f673000p-3, 0x1.0a1a4a356155fp-45},
    {0x1.3a524387ac822p+0, -0x1.a454082e6b000p-3, 0x1.3f4f06fc11d1ep-45},
    {0x1.38d22d366088ep+0, -0x1.9a8778debb000p-3, 0x1.7176b820278e0p-45},
    {0x1.3755bd1c945eep+0, -0x1.90c6db9fcc000p-3, 0x1.929357718d7cap-46},
    {0x1.35dce5f9f2af8p+0, -0x1.871213750f000p-3, 0x1.9b1a97a0ca116p-45},
    {0x1.34679ace01346p+0, -0x1.7d6903caf6000p-3, 0x1.4cd0b17c301d7p-45},
    {0x1.32f5ced6a1dfap+0, -0x1.73cb9074fd000p-3, -0x1.4ca3797ffd2ccp-47},
    {0x1.3187758e9ebb6p+0, -0x1.6a399dabbd000p-3, -0x1.c1aec6657a967p-46},
    {0x1.301c82ac40260p+0, -0x1.60b3100b09000p-3, -0x1.1d1526cee0fd8p-45},
    {0x1.2eb4ea1fed14bp+0, -0x1.5737cc9019000p-3, 0x1.91a01651de028p-46},
    {0x1.2d50a012d50a0p+0, -0x1.4dc7b897bc000p-3, -0x1.c71b60ae1ff0fp-47},
    {0x1.2bef98e5a3711p+0, -0x1.4462b9dc9b000p-3, -0x1.edf3d63b93e7ap-46},
    {0x1.2a91c92f3c105p+0, -0x1.3b08b6757f000p-3, -0x1.53abc35b37c15p-46},
    {0x1.293725bb804a5p+0, -0x1.31b994d3a5000p-3, 0x1.e91238b5efe06p-49},
    {0x1.27dfa38a1ce4dp+0, -0x1.28753bc11b000p-3, 0x1.1797394d9fa33p-45},
    {0x1.268b37cd60127p+0, -0x1.1f3b925f26000p-3, 0x1.5ddee9b083633p-46},
    {0x1.2539d7e9177b2p+0, -0x1.160c8024b2000p-3, -0x1.ebfb2a9009e3dp-45},
    {0x1.23eb79717605bp+0, -0x1.0ce7ecdccc000p-3, -0x1.4588dabff5447p-46},
    {0x1.22a0122a0122ap+0, -0x1.03cdc0a51f000p-3, 0x1.f95cc3a580e90p-46},
    {0x1.21579804855e6p+0, -0x1.f57bc7d900000p-4, -0x1.76a2c9ea8b04ep-46},
    {0x1.2012012012012p+0, -0x1.e3707ee304000p-4, -0x1.0f664e6766abdp-45},
    {0x1.1ecf43c7fb84cp+0, -0x1.d17978821a000p-4, 0x1.93cd894208225p-45},
    {0x1.1d8f5672e4abdp+0, -0x1.bf968769fc000p-4, -0x1.42f7c8d824283p-45},
    {0x1.1c522fc1ce059p+0, -0x1.adc77ee5ae000p-4, -0x1.51cebec79cdf7p-45},
    {0x1.1b17c67f2bae3p+0, -0x1.9c0c32d4d2000p-4, -0x1.5329d85f1e661p-46},
    {0x1.19e0119e0119ep+0, -0x1.8a6477a91e000p-4, 0x1.eba7a83214905p-47},
    {0x1.18ab083902bdbp+0, -0x1.78d02263d8000p-4, -0x1.6bb9794b69fb7p-47},
    {0x1.1778a191bd684p+0, -0x1.674f089366000p-4, 0x1.61f1acd8b33f9p-46},
    {0x1.1648d50fc3201p+0, -0x1.55e10050e0000p-4, -0x1.c13340c53c72ep-47},
    {0x1.151b9a3fdd5c9p+0, -0x1.4485e03dbe000p-4, 0x1.3ec45cb655244p-50},
    {0x1.13f0e8d344724p+0, -0x1.333d7f8184000p-4, 0x1.6c6b6a81b8848p-49},
    {0x1.12c8b89edc0acp+0, -0x1.2207b5c786000p-4, 0x1.6bd2607de7082p-45},
    {0x1.11a3019a74826p+0, -0x1.10e45b3cae000p-4, -0x1.052cdaf6b9737p-45},
    {0x1.107fbbe011080p+0, -0x1.ffa6911ab8000p-5, -0x1.3088c98381a8fp-45},
    {0x1.0f5edfab325a2p+0, -0x1.dda8adc680000p-5, 0x1.1a74c64d9e42fp-45},
    {0x1.0e40655826011p+0, -0x1.bbcebfc690000p-5, 0x1.7b8e68c317c2ap-46},
    {0x1.0d24456359e3ap+0, -0x1.9a187b573c000p-5, -0x1.e810362764de5p-45},
    {0x1.0c0a7868b4171p+0, -0x1.788595a358000p-5, 0x1.06fed083b3a4cp-46},
    {0x1.0af2f722eecb5p+0, -0x1.5715c4c03c000p-5, -0x1.dc2a880ee2760p-46},
    {0x1.09ddba6af8360p+0, -0x1.35c8bfaa14000p-5, 0x1.f2e0a8418532bp-46},
    {0x1.08cabb37565e2p+0, -0x1.149e3e4004000p-5, -0x1.a8ccacb7d2e06p-45},
    {0x1.07b9f29b8eae2p+0, -0x1.e72bf28140000p-6, 0x1.8cb3149774d47p-45},
    {0x1.06ab59c7912fbp+0, -0x1.a55f548c60000p-6, 0x1.dec609f2d03c9p-45},
    {0x1.059eea0727586p+0, -0x1.63d6178690000p-6, -0x1.77b7389596542p-47},
    {0x1.04949cc1664c5p+0, -0x1.228fb1fea0000p-6, -0x1.70513284991fep-45},
    {0x1.038c6b78247fcp+0, -0x1.c317384c70000p-7, -0x1.7c35806208c05p-45},
    {0x1.02864fc7729e9p+0, -0x1.41929f9680000p-7, -0x1.9862755d01368p-46},
    {0x1.0182436517a37p+0, -0x1.8121214580000p-8, -0x1.ac06382973f27p-46},
    {0x1.0080402010080p+0, -0x1.0040155d40000p-9, -0x1.881de70671eefp-45},
    {0x1.fe01fe01fe020p-1, 0x1.ff00aa2b00000p-9, 0x1.0ba04a086b56ap-45},
    {0x1.fa11caa01fa12p-1, 0x1.7dc475f810000p-7, 0x1.4d2ba4a25e0b1p-48},
    {0x1.f6310aca0dbb5p-1, 0x1.3cea443468000p-6, 0x1.2c1e79a52b7eap-45},
    {0x1.f25f644230ab5p-1, 0x1.b9fc027af8000p-6, 0x1.1999bd465b759p-46},
    {0x1.ee9c7f8458e02p-1, 0x1.1b0d98923c000p-5, 0x1.97ea2ca2eec8ap-45},
    {0x1.eae807aba01ebp-1, 0x1.58a5bafc90000p-5, -0x1.b2d039570ad39p-45},
    {0x1.e741aa59750e4p-1, 0x1.95c830ec90000p-5, -0x1.c0dc297c5feb8p-45},
    {0x1.e3a9179dc1a73p-1, 0x1.d276b8adb0000p-5, 0x1.6ac83c78a64b0p-46},
    {0x1.e01e01e01e01ep-1, 0x1.075983598e000p-4, 0x1.1c5006d2999e2p-46},
    {0x1.dca01dca01dcap-1, 0x1.253f62f0a2000p-4, -0x1.7d1ee092cb1fep-45},
    {0x1.d92f2231e7f8ap-1, 0x1.42edcbea64000p-4, 0x1.bb6aeea7c9acdp-46},
    {0x1.d5cac807572b2p-1, 0x1.60658a9376000p-4, -0x1.e787c422c7611p-45},
    {0x1.d272ca3fc5b1ap-1, 0x1.7da766d7b2000p-4, -0x1.a5f9776fe6ecap-45},
    {0x1.cf26e5c44bfc6p-1, 0x1.9ab4246204000p-4, -0x1.8a46826787061p-45},
    {0x1.cbe6d9601cbe7p-1, 0x1.b78c82bb0e000p-4, 0x1.b3f60878cf032p-45},
    {0x1.c8b265afb8a42p-1, 0x1.d4313d66cc000p-4, -0x1.9452379135713p-45},
    {0x1.c5894d10d4986p-1, 0x1.f0a30c0116000p-4, 0x1.5218be64b8b77p-47},
    {0x1.c26b5392ea01cp-1, 0x1.0671512ca6000p-3, -0x1.a44979cdc0a3dp-45},
    {0x1.bf583ee868d8bp-1, 0x1.1478584674000p-3, 0x1.560651027c750p-46},
    {0x1.bc4fd65883e7bp-1, 0x1.2266f190a6000p-3, -0x1.4cddab840e7f6p-45},
    {0x1.b951e2b18ff23p-1, 0x1.303d718e48000p-3, -0x1.5b6b5ce3ecb05p-50},
    {0x1.b65e2e3beee05p-1, 0x1.3dfc2b0ecc000p-3, 0x1.8a9ba62b8c13fp-45},
    {0x1.b37484ad806cep-1, 0x1.4ba36f39a5000p-3, 0x1.79208981bcc36p-45},
    {0x1.b094b31d922a4p-1, 0x1.59338d9982000p-3, 0x1.0ac68b7555d4ap-48},
    {0x1.adbe87f94905ep-1, 0x1.66acd4272b000p-3, -0x1.578c900e4e1ebp-46},
    {0x1.aaf1d2f87ebfdp-1, 0x1.740f8f5403000p-3, 0x1.e8cb6cdfceabep-45},
    {0x1.a82e65130e159p-1, 0x1.815c0a1435000p-3, 0x1.fa48a0dbfc630p-45},
    {0x1.a574107688a4ap-1, 0x1.8e928de887000p-3, -0x1.5f76d3b0a34adp-46},
    {0x1.a2c2a87c51ca0p-1, 0x1.9bb362e7e0000p-3, -0x1.1eca8a1ce0ffcp-45},
    {0x1.a01a01a01a01ap-1, 0x1.a8becfc883000p-3, -0x1.ce6a30de4630ep-48},
    {0x1.9d79f176b682dp-1, 0x1.b5b519e8fb000p-3, 0x1.6962a27fdc19ep-45},
    {0x1.9ae24ea5510dap-1, 0x1.c2968558c2000p-3, -0x1.cf7d3dee38a40p-45},
    {0x1.9852f0d8ec0ffp-1, 0x1.cf6354e09c000p-3, 0x1.775339a07d55bp-45},
    {0x1.95cbb0be377aep-1, 0x1.dc1bca0abf000p-3, -0x1.c2639675ccce9p-46},
    {0x1.934c67f9b2ce6p-1, 0x1.e8c0252aa6000p-3, -0x1.6803b80e8e6ffp-45},
    {0x1.90d4f120190d5p-1, 0x1.f550a564b8000p-3, -0x1.32513a09202fep-45},
    {0x1.8e6527af1373fp-1, 0x1.00e6c45ad5000p-2, 0x1.cd88d52e01203p-50},
    {0x1.8bfce8062ff3ap-1, 0x1.071b85fcd5800p-2, 0x1.0d211707f97bep-46},
    {0x1.899c0f601899cp-1, 0x1.0d46b579ab800p-2, -0x1.696f04df8f0d1p-47},
    {0x1.87427bcc092b9p-1, 0x1.136870293a800p-2, 0x1.6061b314c76e9p-47},
    {0x1.84f00c2780614p-1, 0x1.1980d2dd42000p-2, 0x1.b75fa7a361c9ap-45},
    {0x1.82a4a0182a4a0p-1, 0x1.1f8ff9e48a000p-2, 0x1.7966c040cbe77p-45},
    {0x1.8060180601806p-1, 0x1.2596010df7800p-2, -0x1.c60cf76c57076p-46},
    {0x1.7e225515a4f1dp-1, 0x1.2b9303ab8a000p-2, -0x1.6d8c2d6bfb0a5p-45},
    {0x1.7beb3922e017cp-1, 0x1.31871c9544000p-2, 0x1.84c2b94cecfd9p-46},
    {0x1.79baa6bb6398bp-1, 0x1.3772662bfd800p-2, 0x1.708153ac4fdd0p-48},
    {0x1.77908119ac60dp-1, 0x1.3d54fa5c1f800p-2, -0x1.dfd5932e350e5p-47},
    {0x1.756cac201756dp-1, 0x1.432ef2a04e800p-2, 0x1.2cf9b3a3a94dcp-50},
};
// clang-format on

inline constexpr int kLogFloatTableBits = 6;
// The bits of the float 0x1.66p-1: the start of the range of z.
inline constexpr std::uint32_t kLogFloatOffset = 0x3F330000u;
inline constexpr double kLogFloatLn2 = 0x1.62e42fefa39efp-1;

// The minimax polynomial of log1p(r) - r = r^2 (A0 + A1 r + ... + A4 r^4)
// for the relative error of log1p(r).
inline constexpr double kLogFloatA0 = -0x1.0000000000025p-1;
inline constexpr double kLogFloatA1 = 0x1.5555555164fbdp-2;
inline constexpr double kLogFloatA2 = -0x1.ffffffeed97f1p-3;
inline constexpr double kLogFloatA3 = 0x1.999ff715d0416p-3;
inline constexpr double kLogFloatA4 = -0x1.555d0bf412e6cp-3;

// One interval of z of the float form: 1/c with 29 significant bits, and
// -log of it as a double.
struct LogFloatEntry {
    double invc = 0.0;
    double logc = 0.0;
};

// clang-format off
inline constexpr LogFloatEntry kLogFloatTable[64] = {
    {0x1.6c16c17000000p+0, -0x1.68ac83f4c6a14p-2}, {0x1.6816817000000p+0, -0x1.5d5bde0c15f30p-2},
    {0x1.642c859000000p+0, -0x1.522ae0718a3d8p-2}, {0x1.6058160000000p+0, -0x1.4718dc171c41bp-2},
    {0x1.5c9882c000000p+0, -0x1.3c25278733184p-2}, {0x1.58ed231000000p+0, -0x1.314f1e34b5ce4p-2},
    {0x1.5555555000000p+0, -0x1.269621034db92p-2}, {0x1.51d07eb000000p+0, -0x1.1bf9963b26b95p-2},
    {0x1.4e5e0a7000000p+0, -0x1.1178e8197e47cp-2}, {0x1.4afd6a0000000p+0, -0x1.071385f4d5862p-2},
    {0x1.47ae148000000p+0, -0x1.f991c6eb3b379p-3}, {0x1.446f865000000p+0, -0x1.e530efd771012p-3},
    {0x1.4141414000000p+0, -0x1.d1037f1e55e7bp-3}, {0x1.3e22cbd000000p+0, -0x1.bd08738ebd8adp-3},
    {0x1.3b13b14000000p+0, -0x1.a93ed3e8ad9e3p-3}, {0x1.3813814000000p+0, -0x1.95a5ae037017fp-3},
    {0x1.3521cfb000000p+0, -0x1.823c16431a3c2p-3}, {0x1.323e34a000000p+0, -0x1.6f0128a556abcp-3},
    {0x1.2f684be000000p+0, -0x1.5bf406dd43db2p-3}, {0x1.2c9fb4e000000p+0, -0x1.4913d8693b561p-3},
    {0x1.29e412a000000p+0, -0x1.365fcb0d59016p-3}, {0x1.27350b9000000p+0, -0x1.23d712db9c202p-3},
    {0x1.2492492000000p+0, -0x1.1178e8027e47cp-3}, {0x1.21fb781000000p+0, -0x1.fe89137fbd566p-4},
    {0x1.1f7047e000000p+0, -0x1.da727670446a2p-4}, {0x1.1cf06ae000000p+0, -0x1.b6ac892ed5b1cp-4},
    {0x1.1a7b961000000p+0, -0x1.9335e5bd94989p-4}, {0x1.1811812000000p+0, -0x1.700d3122ac0e1p-4},
    {0x1.15b1e5f000000p+0, -0x1.4d31156607eacp-4}, {0x1.135c811000000p+0, -0x1.2aa04a12717a5p-4},
    {0x1.1111111000000p+0, -0x1.08598b49e3a07p-4}, {0x1.0ecf56c000000p+0, -0x1.ccb73d0ddb2ccp-5},
    {0x1.0c97150000000p+0, -0x1.894aa1c9fb343p-5}, {0x1.0a6810a000000p+0, -0x1.466aec7ade3e9p-5},
    {0x1.0842108000000p+0, -0x1.0415d81e74444p-5}, {0x1.0624dd3000000p+0, -0x1.849252c48cabfp-6},
    {0x1.0410410000000p+0, -0x1.0205648935847p-6}, {0x1.0204081000000p+0, -0x1.010157488de71p-7},
    {0x1.0000000000000p+0, 0.0},                   {0x1.f81f820000000p-1, 0x1.fc0a890fc03e4p-7},
    {0x1.f07c1f0000000p-1, 0x1.f829b1e783300p-6},  {0x1.e9131ac000000p-1, 0x1.77458f532dcfcp-5},
    {0x1.e1e1e1e000000p-1, 0x1.f0a30c21162a6p-5},  {0x1.dae6077000000p-1, 0x1.341d793bbd1d1p-4},
    {0x1.d41d41d000000p-1, 0x1.6f0d28d256b4cp-4},  {0x1.cd85689000000p-1, 0x1.a926d3a6ad563p-4},
    {0x1.c71c71c000000p-1, 0x1.e2707722af2e6p-4},  {0x1.c0e0704000000p-1, 0x1.0d77e7a908e59p-3},
    {0x1.bacf915000000p-1, 0x1.29552f6fff523p-3},  {0x1.b4e81b5000000p-1, 0x1.44d2b6c5b7d1ep-3},
    {0x1.af286bd000000p-1, 0x1.5ff306ee793d4p-3},  {0x1.a98ef60000000p-1, 0x1.7ab890410d909p-3},
    {0x1.a41a41a000000p-1, 0x1.9525a9e3456b4p-3},  {0x1.9ec8e95000000p-1, 0x1.af3c94ed0bff3p-3},
    {0x1.999999a000000p-1, 0x1.c8ff7c59a9a22p-3},  {0x1.948b0fd000000p-1, 0x1.e27076d5af2e6p-3},
    {0x1.8f9c190000000p-1, 0x1.fb9186b5e3e2bp-3},  {0x1.8acb90f000000p-1, 0x1.0a324e38b90e3p-2},
    {0x1.8618618000000p-1, 0x1.1675cacaba60ep-2},  {0x1.8181818000000p-1, 0x1.22941fc0f7966p-2},
    {0x1.7d05f41000000p-1, 0x1.2e8e2bc311d31p-2},  {0x1.78a4c81000000p-1, 0x1.3a64c56b145eap-2},
    {0x1.745d174000000p-1, 0x1.4618bc31c5ec2p-2},  {0x1.702e05c000000p-1, 0x1.51aad874df82dp-2},
};
// clang-format on

// The tolerance of the rounding test of the float fast path, in units of the
// last bit of the double result: the error 2^-48 of the fast path times 2^53,
// with a margin of eight.
inline constexpr std::uint64_t kLogFloatTolerance = std::uint64_t{1} << 8;

inline namespace CRUCIBLE_SIMD_TARGET {

// log of x in [1 - 2^-4, 1 + 0x1.09p-4], where r = x - 1 is exact.
[[nodiscard]] constexpr double log_near_one_(double value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    double const reduced = value - 1.0;
    double const square = reduced * reduced;
    double const cube = reduced * square;
    double const first = fma_of_finite(square, kLogB3, fma_of_finite(reduced, kLogB2, kLogB1));
    double const second = fma_of_finite(square, kLogB6, fma_of_finite(reduced, kLogB5, kLogB4));
    double const third =
        fma_of_finite(cube, kLogB10, fma_of_finite(square, kLogB9, fma_of_finite(reduced, kLogB8, kLogB7)));
    double const tail = fma_of_finite(cube, fma_of_finite(cube, third, second), first);
    // -r^2/2 in two parts: the round of r^2 and its exact error, each times -1/2.
    double const half_square_high = square * -0.5;
    double const half_square_low = fma_of_finite(reduced, reduced, -square) * -0.5;
    double const high = reduced + half_square_high;
    double const low = ((reduced - high) + half_square_high) + half_square_low;
    return fma_of_finite(cube, tail, low) + high;
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
    using ::fixy::fp::detail::fma_of_finite;
    std::uint64_t bits = __builtin_bit_cast(std::uint64_t, value);
    if (bits - detail::kLogNearLow < detail::kLogNearHigh - detail::kLogNearLow) {
        return detail::log_near_one_(value);
    }
    std::uint64_t const top = bits >> 48;
    if (top - 0x0010u >= 0x7ff0u - 0x0010u) [[unlikely]] {
        // x below 2^-1022, negative, an infinity or a NaN.
        if ((bits << 1) == 0) return -::foundation::core::infinity_of<double>;
        if (bits == 0x7ff0000000000000ULL) return value;
        if ((top & 0x8000u) != 0 || (top & 0x7ff0u) == 0x7ff0u) return ::foundation::core::quiet_nan_of<double>;
        // A positive subnormal x: the shift puts its leading bit at bit 52,
        // and the subtract gives the biased exponent 1 - shift, which is zero
        // or negative and wraps modulo 2^64.  The steps below read that
        // exponent through an arithmetic shift.
        auto const shift = static_cast<std::uint64_t>(__builtin_clzll(bits) - 11);
        bits = (bits << shift) - (shift << 52);
    }
    std::uint64_t const offset_bits = bits - detail::kLogOffset;
    std::uint64_t const index = (offset_bits >> (52 - detail::kLogTableBits)) & 127u;
    std::int64_t const exponent = static_cast<std::int64_t>(offset_bits) >> 52;
    double const reduced_input = __builtin_bit_cast(double, bits - (offset_bits & (std::uint64_t{0xfff} << 52)));
    detail::LogEntry const& entry = detail::kLogTable[index];
    double const reduced = fma_of_finite(reduced_input, entry.invc, -1.0);
    double const scale = static_cast<double>(exponent);
    double const whole = fma_of_finite(scale, detail::kLogLn2Hi, entry.logc_high);
    double const high = whole + reduced;
    double const low = ((whole - high) + reduced) + fma_of_finite(scale, detail::kLogLn2Lo, entry.logc_low);
    double const square = reduced * reduced;
    double const upper = fma_of_finite(reduced, detail::kLogA4, detail::kLogA3);
    double const lower = fma_of_finite(reduced, detail::kLogA2, detail::kLogA1);
    double const tail = fma_of_finite(square, upper, lower);
    double result = fma_of_finite(square, detail::kLogA0, low);
    result = fma_of_finite(reduced * square, tail, result);
    return result + high;
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
    std::uint32_t const index = (offset_bits >> (23 - detail::kLogFloatTableBits)) & 63u;
    std::int32_t const exponent = static_cast<std::int32_t>(offset_bits) >> 23;
    float const reduced_input = __builtin_bit_cast(float, bits - (offset_bits & 0xff800000u));
    detail::LogFloatEntry const& entry = detail::kLogFloatTable[index];
    // z has 24 bits and invc has 29, so the product is exact, and the
    // subtract of 1 is exact.
    // So an fma gives the same exact r as the multiply and the subtract, two
    // cycles sooner.  At x86-64 and x86-64-v2 the fma is a form in doubles
    // of about 100 instructions, so the multiply and the subtract stay there.
#if defined(__FMA__) || defined(__aarch64__)
    double const reduced = fma_of_finite(static_cast<double>(reduced_input), entry.invc, -1.0);
#else
    double const reduced = static_cast<double>(reduced_input) * entry.invc - 1.0;
#endif
    double const whole = fma_of_finite(static_cast<double>(exponent), detail::kLogFloatLn2, entry.logc);
    // A0 + A1 r + A2 r^2 + A3 r^3 + A4 r^4 in the form of Estrin: three fma
    // deep after r^2, where the form of Horner is four fma deep after r.
    double const square = reduced * reduced;
    double const low = fma_of_finite(reduced, detail::kLogFloatA1, detail::kLogFloatA0);
    double const high =
        fma_of_finite(square, detail::kLogFloatA4, fma_of_finite(reduced, detail::kLogFloatA3, detail::kLogFloatA2));
    double const tail = fma_of_finite(square, high, low);
    double const result = whole + fma_of_finite(square, tail, reduced);
    if (detail::is_near_float_midpoint_(result, detail::kLogFloatTolerance)) [[unlikely]] {
        return detail::log_float_slow_(value);
    }
    return static_cast<float>(result);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp
