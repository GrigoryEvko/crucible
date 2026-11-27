#pragma once

// erf of a double and of a float, with the same bits on each target and each
// level of the instruction set.
//
//   fixy::fp::erf(double)   error of at most 0.8 units in the last place
//   fixy::fp::erf(float)    the correctly rounded result for each input
//
// Each function is total and odd: erf(-x) = -erf(x) for each x, ±0 keep their
// sign, and ±inf give ±1.  A NaN gives the canonical quiet NaN
// quiet_nan_of<T>.  No function reads or writes errno or a flag of the
// floating-point unit on purpose.  In each function, a signaling NaN gets to
// one quiet compare, which raises the invalid flag, as IEEE 754 states for an
// operation on a signaling NaN.
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
//   detail::fma_of_finite, integer operations on the bits, table reads and one conversion
//   of a bounded value to an integer, in the order of the source, under the
//   floor crucible_fp_strict (-ffp-contract=off).  The functions are in the
//   inline namespace CRUCIBLE_SIMD_TARGET.
//
// Algorithm of erf(double): piecewise polynomials.
//   |x| < 2^-28: erf(x) = 2/sqrt(pi) x, with 2/sqrt(pi) in two parts and one
//     fma.  A subnormal x takes the fma directly, which rounds the result one
//     time at its own precision.  A normal x takes it at the scale 2^64, and
//     the multiply by 2^-64 is exact, because the result is normal.
//   |x| < 1/2: erf(x) = x (2/sqrt(pi) + z P(z)) with z = x^2.  P is the
//     minimax polynomial of degree 9 for the relative error of erf, with an
//     error of 2^-58.3.  The last fma adds the high part of 2/sqrt(pi) times x
//     to x times the tail with one round.
//   1/2 <= |x| < 5.9215871957945: 44 intervals of width 1/8.  On each, erf is
//     the Taylor polynomial of degree 11 at the center c: erf(c) in two parts,
//     plus t (c1 + c2 t + ... + c11 t^10) with t = |x| - c exact.  The largest
//     absolute error of a table polynomial is 2^-58.8.
//   |x| >= 5.9215871957945: ±1, the round of erf(x) for each such x.
//   The fma sites: two for a tiny x (one in each branch), three below 1/2 (the
//   step of the Horner loop, the tail and the last fma), and two in an
//   interval (the step of the Horner loop and the last fma).
//
// Algorithm of erf(float)
//   The form computes in double and rounds the double result to a float one
//   time, with no rounding test and no slow path.
//   |x| < 1/2: erf(x) = x Q(z) with z = x^2.  Q0 is the double nearest
//     2/sqrt(pi), and Q1 to Q7 are the minimax polynomial for the relative
//     error of erf, with an error of 2^-50.4.
//   1/2 <= |x| < 0x1.f5a88ap+1: 55 intervals of width 1/16 in kErfFloatTable.
//     On each, erf is the minimax polynomial of degree 7 in t = |x| - c for
//     the relative error, with an error of 2^-52.4.  t is exact.
//   |x| >= 0x1.f5a88ap+1 gives ±1.
//   The two polynomials take the form of Estrin.  The fma sites: the seven of
//   the form of Estrin below 1/2, and the fma of t and the seven of the form
//   of Estrin in an interval.  The test test_fp_erf_float_exhaustive of each
//   Release build of the gate proves the correct round of each of the 2^32
//   inputs, against the round of erf(double).  The sweep of bench_fp_sweep
//   against CORE-MATH proves it too.
//
// Tables
//   `python3 utils/tools/fp_tables.py erf` prints each table and constant of
//   the double path, and `python3 utils/tools/fp_tables.py erff` prints Q and
//   kErfFloatTable, with the pinned mpmath.

#include <foundation/Platform.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <fixy/fp/FmaOfFinite.h>

#include <cstddef>
#include <cstdint>

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "fixy/fp/Erf.h needs the floor crucible_fp_strict (-ffp-contract=off). Without it the compiler can fuse a multiply and an add, and the bits change with the level."
#endif

namespace fixy::fp::detail {

// 2/sqrt(pi) in two parts.
inline constexpr double kErfTwoOverRootPiHigh = 0x1.20dd750429b6dp+0;
inline constexpr double kErfTwoOverRootPiLow = 0x1.1ae3a914fed80p-56;

// P(z) = P0 + P1 z + ... + P9 z^9: erf(x) = x (2/sqrt(pi) + z P(z)) with
// z = x^2, the minimax polynomial on [0, 1/4] for the relative error of erf.
inline constexpr double kErfP[10] = {
    -0x1.812746b0379e7p-2,  0x1.ce2f21a042be1p-4,  -0x1.b82ce31288a16p-6,  0x1.565bcd0e60199p-8,
    -0x1.c02db3fa2362ep-11, 0x1.f9a324b661370p-14, -0x1.f4d1d2ffb7588p-17, 0x1.b9d1eb7a56a9ap-20,
    -0x1.5d7aae19be1e9p-23, 0x1.c63cb933ca664p-27,
};

// The first x with erf(x) rounded to 1: in double and in float.
inline constexpr double kErfOneFrom = 0x1.7afb48dc96627p+2;
inline constexpr float kErfFloatOneFrom = 0x1.f5a88ap+1f;

inline constexpr int kErfIntervals = 44;
inline constexpr int kErfDegree = 11;
// The degree of P below 1/2, and the degrees of the float form, in the
// intervals of kErfFloatTable and of Q in z.
inline constexpr int kErfSmallDegree = 9;
inline constexpr int kErfFloatDegree = 7;
inline constexpr int kErfFloatSmallDegree = 7;

// Q(z) = Q0 + Q1 z + ... + Q7 z^7: erf(x) = x Q(z) with z = x^2 below 1/2,
// for the float form.  Q0 is the double nearest 2/sqrt(pi), and Q1 to Q7 are
// the minimax polynomial on [0, 1/4] for the relative error of erf.  From
// python3 utils/tools/fp_tables.py erff.
inline constexpr double kErfFloatQ[kErfFloatSmallDegree + 1] = {
    0x1.20dd750429b6dp+0, -0x1.812746b03685ep-2,  0x1.ce2f219e85240p-4,  -0x1.b82ce222a5cf1p-6,
    0x1.565b8fb485cebp-8, -0x1.c01ce0209db9bp-11, 0x1.f716e492429eep-14, -0x1.c11c77da5bbadp-17,
};

// One interval [1/2 + i/8, 1/2 + (i + 1)/8): erf at its center in two parts,
// then the Taylor coefficients c1 to c11 at the center.
struct ErfInterval {
    double value_high = 0.0;
    double value_low = 0.0;
    double terms[kErfDegree] = {};
};

// clang-format off
inline constexpr ErfInterval kErfTable[kErfIntervals] = {
    {0x1.25b8a88b6dd7fp-1, 0x1.9534a3b5bd215p-55,
     {0x1.a5074e2157620p-1, -0x1.d9a837e5824e4p-2, -0x1.9c41d1d5fae55p-4, 0x1.75bebc1b18d1cp-3,
      -0x1.6410ad9332666p-7, -0x1.7df8890b11fa7p-5, 0x1.4a54816d3608ap-7, 0x1.18f36eb18f3d7p-7,
      -0x1.8d661c030e173p-9, -0x1.3628ede23de1dp-10, 0x1.438eb2b3c46d1p-11}},
    {0x1.569243d2b3a9bp-1, 0x1.8eef7012e8df5p-56,
     {0x1.681ff24b4ab04p-1, -0x1.ef2bed2786b25p-2, -0x1.a4254557d722fp-7, 0x1.532415c267962p-3,
      -0x1.558b4c55a835cp-5, -0x1.1b7ad5b777f1bp-5, 0x1.1201d3bd0e758p-6, 0x1.2995e3a88a890p-8,
      -0x1.0294c3e93cdb0p-8, -0x1.159644a564e28p-12, 0x1.63daf9b385638p-11}},
    {0x1.7fb9bfaed8078p-1, 0x1.66cf14bcad032p-56,
     {0x1.2a8dcede3673bp-1, -0x1.e5267029187c0p-2, 0x1.fe0796bb9d05ap-5, 0x1.0fa23021ad0acp-3,
      -0x1.fa21ebca76761p-5, -0x1.31546d5c4edb4p-6, 0x1.37e5469efb7a6p-6, 0x1.097966e2e87e3p-13,
      -0x1.e82ab020887a7p-9, 0x1.318270c11b2d8p-11, 0x1.12652e433d763p-11}},
    {0x1.a1551a16aaeafp-1, 0x1.a558a46df5f67p-57,
     {0x1.dfca26f5bbf88p-2, -0x1.c1cd84866038fp-2, 0x1.e4c9975da0987p-4, 0x1.747e31bf47af3p-4,
      -0x1.1d1f00109e42ap-4, -0x1.47654175ceb42p-9, 0x1.1a817c594b8cbp-6, -0x1.cb8acd699cca6p-9,
      -0x1.57b72bf874db6p-9, 0x1.24493dca8b643p-10, 0x1.f556774c6a909p-13}},
    {0x1.bbef0fbde6221p-1, -0x1.322c1148e0d48p-55,
     {0x1.75a91a7f4d2edp-2, -0x1.8d03ac274201cp-2, 0x1.3954778d6a0dfp-3, 0x1.88e0f7b183fc6p-5,
      -0x1.0f7c15f75ee13p-4, 0x1.5e22cfa1aab51p-7, 0x1.9ad28c5557c22p-7, -0x1.704ec5d29fc83p-8,
      -0x1.23360304f19bap-10, 0x1.43ca3fcdf057ap-10, -0x1.dcb97a9e067eep-15}},
    {0x1.d0580b2cfd249p-1, 0x1.4fca6318dfee9p-55,
     {0x1.1a0dc51a9934dp-2, -0x1.4ef05a0f95eebp-2, 0x1.5648b5dc47417p-3, 0x1.40fbaba44504cp-7,
      -0x1.c0db89d0a41a4p-5, 0x1.388c3ec056942p-6, 0x1.aecb7463cf446p-8, -0x1.8bca53327e075p-8,
      0x1.4add4a8239f4bp-12, 0x1.05ce4abd0ff4fp-10, -0x1.183f198a0ba0dp-12}},
    {0x1.df85ea8db188ep-1, -0x1.f71e8254d11a9p-55,
     {0x1.9cb5bd549b111p-3, -0x1.0ed7443f85c33p-2, 0x1.5066cda84bba9p-3, -0x1.419fa10b6ed7dp-6,
      -0x1.3f41761d5a941p-5, 0x1.6d1d724baaae4p-6, 0x1.e377f5703f7ffp-11, -0x1.4cc916ad63c27p-8,
      0x1.553ef0d12719fp-10, 0x1.26240f5597ee0p-11, -0x1.6bbf0fffb70d1p-12}},
    {0x1.ea7730ed0bbb9p-1, 0x1.2c5bd7ce1388bp-55,
     {0x1.24a7b84d38971p-3, -0x1.a4b118ef01593p-3, 0x1.319c7a75f9187p-3, -0x1.3db5bed47faf6p-5,
      -0x1.7019bda6c2fddp-6, 0x1.59d3aa402c32ep-6, -0x1.b324eab9c87a9p-9, -0x1.b4774d37d0dd7p-9,
      0x1.c01377485a844p-10, 0x1.a5db5f627b4cdp-14, -0x1.40d9c429b88a4p-12}},
    {0x1.f21c9f12f0677p-1, -0x1.7efe429672266p-58,
     {0x1.92470a61b6965p-4, -0x1.3a47801c56a57p-3, 0x1.0453f90d3bd35p-3, -0x1.8a7c6a239217bp-5,
      -0x1.075c088031ee3p-7, 0x1.16f9c9c127b80p-6, -0x1.74c2fc9bdfe97p-8, -0x1.760c522bd5bebp-10,
      0x1.a3cdb656adb43p-10, -0x1.02c3c1ab0b16dp-12, -0x1.92892013c6fc1p-13}},
    {0x1.f74a6d9a38383p-1, 0x1.c33a329423946p-55,
     {0x1.0bf97e95f2a64p-4, -0x1.c435059d09788p-4, 0x1.a3687c1eaf1adp-4, -0x1.9647a30b16824p-5,
      0x1.6981061dfbb09p-9, 0x1.7e8755da47040p-7, -0x1.9be731fdab95dp-8, 0x1.3a95ae0a75546p-13,
      0x1.319f780e962d7p-10, -0x1.b88dd51a4f7afp-12, -0x1.037f168a8e0c6p-14}},
    {0x1.fab0dd89d1309p-1, -0x1.ae61bd9db1babp-55,
     {0x1.5a08e85af27e0p-5, -0x1.399812926bc23p-4, 0x1.4140efb719cb0p-4, -0x1.7535a61a4193dp-5,
      0x1.374c88c7e6abdp-7, 0x1.a40709e010e77p-8, -0x1.6dc078888efa7p-8, 0x1.2ee6d200993afp-10,
      0x1.44f175e22a161p-11, -0x1.c2fb051c92966p-12, 0x1.523035ed39813p-15}},
    {0x1.fcdacca0bfb73p-1, -0x1.2c33d88729e43p-55,
     {0x1.b1160991ff737p-6, -0x1.a38d59456f77dp-5, 0x1.d5bd91b6b0123p-5, -0x1.3b35dcbc80146p-5,
      0x1.9d76b0a0535c7p-7, 0x1.14c887a83a0e6p-9, -0x1.117f42cc6e9f4p-8, 0x1.9b477bdad8e0bp-10,
      0x1.1d219fb0e1bc9p-13, -0x1.5bb59d3ca6abfp-12, 0x1.8ca373c577165p-14}},
    {0x1.fe307f2b503d0p-1, -0x1.8a555000387f8p-57,
     {0x1.06ae13b0d3255p-6, -0x1.0ee3844e59be7p-5, 0x1.48b127f8ed8a5p-5, -0x1.f155b4e7d8c3bp-6,
      0x1.aa2c0753d569ap-7, -0x1.bbf7e2795837bp-11, -0x1.5478d784d271cp-9, 0x1.8eae08cdf9547p-10,
      -0x1.92946556037e5p-13, -0x1.90f27ae615128p-13, 0x1.b076b7853882ep-14}},
    {0x1.fefcce6813974p-1, -0x1.b27cf5025d1c8p-58,
     {0x1.34d7dbc76d7e5p-7, -0x1.51cc18621fc23p-6, 0x1.b925a99886bb7p-6, -0x1.71e7d408c8c6fp-6,
      0x1.7ea58080a81efp-7, -0x1.46eb9d203e071p-9, -0x1.403333682fa5ep-10, 0x1.3b37d5bd14a40p-10,
      -0x1.6be130822dbdfp-12, -0x1.03d4bcdafe930p-14, 0x1.55848476c7d88p-14}},
    {0x1.ff733814af88cp-1, 0x1.0a87238cea4f9p-56,
     {0x1.5ff2750fe7820p-8, -0x1.96f0575a63ae5p-7, 0x1.1c5a643f04363p-6, -0x1.04f5caaf2196fp-6,
      0x1.382a146afb9d2p-7, -0x1.95cab93aa68d2p-9, -0x1.d2fd90fe62928p-13, 0x1.9f50fb94c0b82p-11,
      -0x1.7d7378074399bp-12, 0x1.cc0c9cba01378p-16, 0x1.92a3a2947141ep-15}},
    {0x1.ffb5bdf67fe6fp-1, 0x1.4e830346f6e7fp-62,
     {0x1.84ba3004a50d0p-9, -0x1.d9c2ea85a927dp-8, 0x1.60898536e104ap-7, -0x1.5eb1c899f0b70p-7,
      0x1.d854f73e74c87p-8, -0x1.897719a9d257ep-9, 0x1.88cdc8b807c97p-12, 0x1.b325a11c1f45ap-12,
      -0x1.381548f692740p-12, 0x1.2b1fd05559d5ap-14, 0x1.1ed31cd6fe717p-16}},
    {0x1.ffd9f78c7524ap-1, 0x1.04ed6ff98e45dp-55,
     {0x1.a024365f771bdp-10, -0x1.0a9732d5284ddp-8, 0x1.a4bf47a43042ap-8, -0x1.c23802d8a5bb7p-8,
      0x1.4f40070668329p-8, -0x1.4c9a2c9dccd04p-9, 0x1.4f7a50b5bc019p-11, 0x1.18b04eb90c73ap-13,
      -0x1.a4c3880c0ea6ap-13, 0x1.4b7b82a86e254p-14, -0x1.0bc762b1be79cp-18}},
    {0x1.ffed167b12ac2p-1, -0x1.ddc0ce3ed8fcbp-55,
     {0x1.afc85e0f82e12p-11, -0x1.221a9f326bef4p-9, 0x1.e3c9aab90bcf4p-9, -0x1.14b1b98141f21p-8,
      0x1.c1c19b9e63d70p-9, -0x1.feac3dbeb5124p-10, 0x1.63e88178b0e49p-11, -0x1.4441c86c93f7ap-15,
      -0x1.c8ceebc5fc50cp-14, 0x1.125b77a79fcb6p-14, -0x1.da7be990bb4aep-17}},
    {0x1.fff6dee89352ep-1, 0x1.b96c0ba13851dp-55,
     {0x1.b23a5a23e4210p-12, -0x1.315107613c673p-10, 0x1.0c243329a9ca1p-9, -0x1.4630116262084p-9,
      0x1.1e84d1022e8cbp-9, -0x1.6b41872716325p-10, 0x1.36edde582b265p-11, -0x1.f7870ebc38e66p-14,
      -0x1.51ecfdc37801ep-15, 0x1.711d817e07bc3p-15, -0x1.0ae90d500cdaap-16}},
    {0x1.fffbb8f1049c6p-1, 0x1.d2c6266b51f27p-56,
     {0x1.a740684026555p-13, -0x1.36d34c8f1c26ap-11, 0x1.1eb6e14974a25p-10, -0x1.714eb8cc0947fp-10,
      0x1.5bec08c01b1d7p-10, -0x1.e4621d82dad12p-11, 0x1.e1b7b564b0e79p-12, -0x1.24564b69716a0p-13,
      0x1.bf8e3b47f3cc9p-20, 0x1.8f55a9be0e03dp-16, -0x1.b3b76e620315ap-17}},
    {0x1.fffe0e0140857p-1, -0x1.6aa36f86c14ddp-57,
     {0x1.8fdc1b2dcf7b9p-14, -0x1.322484cf12daap-12, 0x1.27dc1bc6cfef5p-11, -0x1.9202f465eb421p-11,
      0x1.93b4c9746835fp-11, -0x1.30e9e6142fe9bp-11, 0x1.555b9d5fb4825p-12, -0x1.055983c4ac7abp-13,
      0x1.68e6c75a5d068p-16, 0x1.2d4a50d2829b7p-17, -0x1.1de08b5647c04p-17}},
    {0x1.ffff2436a21dcp-1, -0x1.3607959a29d36p-55,
     {0x1.6e2367dc27f95p-15, -0x1.23c436c36fdabp-13, 0x1.26bf00867a835p-12, -0x1.a51fb50b15f22p-12,
      0x1.c0825378fda08p-12, -0x1.6c3dbfe0cbe4ap-12, 0x1.c1dd1438378dfp-13, -0x1.94c36a9d7c0f6p-14,
      0x1.bf0aab116ca42p-16, 0x1.6bdbd2f94d9afp-23, -0x1.2b32e8d43f559p-18}},
    {0x1.ffffa1de8c582p-1, 0x1.832540129302ap-55,
     {0x1.44f21e49054f2p-16, -0x1.0d18811478659p-14, 0x1.1b964d438f622p-13, -0x1.a8d7851f26bf0p-13,
      0x1.ddd6df9b6852dp-13, -0x1.9e52b7aac1644p-13, 0x1.165b2034fcab2p-13, -0x1.1b75c33326751p-14,
      0x1.91a253c42f4e8p-16, -0x1.020b498059edbp-18, -0x1.ade63f3082a14p-20}},
    {0x1.ffffd8e1a2f22p-1, -0x1.c10adf6b19989p-55,
     {0x1.1783ceac28910p-17, -0x1.e06a8b37e5b93p-16, 0x1.07978c7b8496bp-14, -0x1.9d039884f8be5p-14,
      0x1.e8d1145e94a54p-14, -0x1.c1f7251172a87p-14, 0x1.458b9e0854d68p-14, -0x1.6eb05572453f5p-15,
      0x1.33045cf65279ep-16, -0x1.42c8adf1cd1fdp-18, 0x1.91109b80a7a7ep-27}},
    {0x1.fffff039f9e8fp-1, -0x1.9d1bcd6174e99p-55,
     {0x1.d21397ead99cbp-19, -0x1.9f19734d29cf9p-17, 0x1.d982bd41d8954p-16, -0x1.8320fc4836be5p-15,
      0x1.e0a1cb1d071f3p-15, -0x1.d384223047b9cp-15, 0x1.696daf6422bd4p-15, -0x1.bb6e2d311a919p-16,
      0x1.a4fcb0ea87efbp-17, -0x1.1c940c531c53bp-18, 0x1.7469913f4f763p-21}},
    {0x1.fffff9d446cccp-1, -0x1.bb06bab98bc7ep-57,
     {0x1.789fb715aae95p-20, -0x1.5b333cc7f98f1p-18, 0x1.9b12fdbf90f62p-17, -0x1.5e06923144d70p-16,
      0x1.c6a071925631dp-16, -0x1.d178cb0388a82p-16, 0x1.7e29d33ac92b6p-16, -0x1.f9203429ba9ffp-17,
      0x1.094dadeee395cp-17, -0x1.a771cf3589991p-19, 0x1.b8fd1c29c263fp-21}},
    {0x1.fffffda86faa9p-1, -0x1.d230252d68f25p-56,
     {0x1.26f9df8519bd7p-21, -0x1.1926290adc888p-19, 0x1.5900c02d97304p-18, -0x1.3166de6a8c640p-17,
      0x1.9dfcc328729e0p-17, -0x1.bcab1ed5ec38dp-17, 0x1.81cd74a57ce17p-17, -0x1.106e95b6bf43dp-17,
      0x1.379625a71385fp-18, -0x1.1970a5b67012cp-19, 0x1.74761c833476cp-21}},
    {0x1.ffffff233ee1dp-1, 0x1.db123ed17221dp-55,
     {0x1.bfd7555a3bd68p-23, -0x1.b8d7f804d2e73p-21, 0x1.17f93e5149289p-19, -0x1.013b0457d08fap-18,
      0x1.6b245d7e1d829p-18, -0x1.98077548c6950p-18, 0x1.7492048ab3cebp-18, -0x1.17506c7b39df8p-18,
      0x1.57e94a4c5f5a5p-19, -0x1.5709711f3cabcp-20, 0x1.0a0f9569482a8p-21}},
    {0x1.ffffffb127525p-1, 0x1.504f382db4102p-55,
     {0x1.4980cb3c80949p-24, -0x1.4ea6ce697296fp-22, 0x1.b771d9b6f07b8p-21, -0x1.a26c653fad5b8p-20,
      0x1.3302bb89379dep-19, -0x1.67f42e5264333p-19, 0x1.58b4adafb958ep-19, -0x1.10f5767962c5ap-19,
      0x1.66ca44250dd07p-20, -0x1.84ee0ad7abf19p-21, 0x1.53b60652920e7p-22}},
    {0x1.ffffffe4aed5ep-1, 0x1.389c0f32ad0f4p-59,
     {0x1.d5f3a8dea7357p-26, -0x1.ebfb14c9170c0p-24, 0x1.4d9228525f449p-22, -0x1.48b536addac5fp-21,
      0x1.f48ccf23a68e2p-21, -0x1.3183b6134cf04p-20, 0x1.31efde2215f01p-20, -0x1.fd9eeb0f18631p-21,
      0x1.63414459ae298p-21, -0x1.9dda81c133f08p-22, 0x1.8da7d30642544p-23}},
    {0x1.fffffff6d1e56p-1, -0x1.64d969b4be4c4p-55,
     {0x1.44d26de513197p-27, -0x1.5e32de7af8977p-25, 0x1.e9e05b3c8f38ap-24, -0x1.f2f6fa7db5b1dp-23,
      0x1.899dcace485ebp-22, -0x1.f34b7eef3c9b1p-22, 0x1.04be030272d14p-21, -0x1.c73bd2257171ep-22,
      0x1.4edda838439f6p-22, -0x1.9fc860b474229p-23, 0x1.b0d686a25ff7cp-24}},
    {0x1.fffffffd01f89p-1, -0x1.35e8e39884f62p-56,
     {0x1.b334fac4b9f99p-29, -0x1.e2cec6323e50ep-27, 0x1.5c027d5bba36ap-25, -0x1.6df4d024fffbep-24,
      0x1.2aaf7c205b9eap-23, -0x1.8902edfbfefd6p-23, 0x1.ab2ab1b338249p-23, -0x1.85abe0ff1ba49p-23,
      0x1.2d32f7c3621ecp-23, -0x1.8c141c67361ccp-24, 0x1.b9fa6fbb9a95ap-25}},
    {0x1.ffffffff0dd2bp-1, 0x1.0df73e7d2fc98p-55,
     {0x1.1a94ff571654fp-30, -0x1.4251f33f5578fp-28, 0x1.de6bc1f75bb9bp-27, -0x1.036b5fd1c4158p-25,
      0x1.b58f1385def96p-25, -0x1.2a2347efb2135p-24, 0x1.508db866ffe00p-24, -0x1.3ffea93467fbfp-24,
      0x1.02ff87b2e2577p-24, -0x1.66e54eb04652dp-25, 0x1.a9ea2195c4985p-26}},
    {0x1.ffffffffb5be5p-1, -0x1.729d6819c7f34p-56,
     {0x1.63ac6b4edc88ep-32, -0x1.a0ce0dc06a706p-30, 0x1.3e380dd7593a5p-28, -0x1.638bc4fb02cbap-27,
      0x1.35753ad4c5875p-26, -0x1.b41f33cafccc9p-26, 0x1.fe694e371a659p-26, -0x1.f8af0121a5e7cp-26,
      0x1.aa77274dab3dbp-26, -0x1.3616fe99f19f3p-26, 0x1.84fddf4c674dcp-27}},
    {0x1.ffffffffe9eb0p-1, -0x1.ea527e0bef1ecp-58,
     {0x1.b1e5acf351d87p-34, -0x1.05042a0a5f3c3p-31, 0x1.99ac8fd63c66cp-30, -0x1.d72344378e114p-29,
      0x1.a6be9a123435bp-28, -0x1.33aacb4bf6ce3p-27, 0x1.74b732e7ceaa7p-27, -0x1.7e7eab6578e6ap-27,
      0x1.50959f2daae3bp-27, -0x1.ffed4b859bd78p-28, 0x1.51c7f99f8fda3p-28}},
    {0x1.fffffffff9a1bp-1, -0x1.6a87270d2450ep-57,
     {0x1.0084ff125639dp-35, -0x1.3ca42adaa26f6p-33, 0x1.fe73513c67bf8p-32, -0x1.2dd9aa5a2bee4p-30,
      0x1.16ef6b93944a8p-29, -0x1.a2d58e9b2278bp-29, 0x1.06389b9748f25p-28, -0x1.16cdd9ebd5c76p-28,
      0x1.fdd861b55c502p-29, -0x1.945781eff3c56p-29, 0x1.178f3905f3e13p-29}},
    {0x1.fffffffffe380p-1, 0x1.7ce07114e4fe0p-55,
     {0x1.25f9ee0b923dcp-37, -0x1.74105146a5162p-35, 0x1.33cde4f35d941p-33, -0x1.760fe7b666392p-32,
      0x1.63a70fd66d485p-31, -0x1.1324f6fb6decfp-30, 0x1.63a31a36b815cp-30, -0x1.8724ca89a96d5p-30,
      0x1.72e290891e5dep-30, -0x1.31fc02f5342e7p-30, 0x1.b9e8b0e7fa322p-31}},
    {0x1.ffffffffff845p-1, 0x1.b0edc5a89ab8fp-56,
     {0x1.46897d4b69fc6p-39, -0x1.a77a4e7dcd735p-37, 0x1.67543695dcc12p-35, -0x1.c05c1e2fc7105p-34,
      0x1.b639419fedf8ep-33, -0x1.5cfd7eb9c1025p-32, 0x1.d11578959ba45p-32, -0x1.082f9e9d1bf57p-31,
      0x1.0354ceadad8afp-31, -0x1.bc2dfa2690a64p-32, 0x1.4e11efdc68123p-32}},
    {0x1.ffffffffffdf8p-1, -0x1.dcf8b10ff973bp-55,
     {0x1.5f8b87a31bd85p-41, -0x1.d2e55024a0fb5p-39, 0x1.9612cc225df4bp-37, -0x1.03ee5f38b9b49p-35,
      0x1.04f2f71e2e96bp-34, -0x1.ab7099f99e01bp-34, 0x1.2554b8f609fd1p-33, -0x1.57c8752774563p-33,
      0x1.5cd182c967665p-33, -0x1.3580a8444f948p-33, 0x1.e3be72b1c25e0p-34}},
    {0x1.fffffffffff7bp-1, 0x1.00fa07f7fb612p-55,
     {0x1.6ed2f2515e933p-43, -0x1.f2a6c1669c902p-41, 0x1.bc42ba38a13f8p-39, -0x1.2391e135afab8p-37,
      0x1.2c6c24550f64fp-36, -0x1.f9a3c1b0e1479p-36, 0x1.6502546ab341ap-35, -0x1.af22316e181c7p-35,
      0x1.c388dd1764f21p-35, -0x1.9e65df1db0f56p-35, 0x1.4fcd27877d440p-35}},
    {0x1.fffffffffffdfp-1, 0x1.5669e670f914bp-56,
     {0x1.72fd93e036cdcp-45, -0x1.01f450d1e61b1p-42, 0x1.d68fb81b2ed89p-41, -0x1.3c706aa4d2517p-39,
      0x1.4e6479565838ep-38, -0x1.20e9eb8375e6dp-37, 0x1.a35b9d2fcac80p-37, -0x1.04a1357d2538ap-36,
      0x1.196579f27dd9bp-36, -0x1.0ab824e9a83c6p-36, 0x1.bf68355f5f78ap-37}},
    {0x1.ffffffffffff8p-1, 0x1.0160ef15c497dp-56,
     {0x1.6ba91ac734786p-47, -0x1.028a39099f4d9p-44, 0x1.e292863e1795ep-43, -0x1.4c4e690fbe215p-41,
      0x1.67e6e5ac60fd1p-40, -0x1.3f00d80a59edbp-39, 0x1.db88ee63eb28bp-39, -0x1.2fe58b79ed432p-38,
      0x1.51dbeae22a581p-38, -0x1.4a49e1ac4c62ep-38, 0x1.1e432d67585a3p-38}},
    {0x1.ffffffffffffep-1, 0x1.59ab24e589a30p-56,
     {0x1.5982008db1304p-49, -0x1.f610e8cde57a1p-47, 0x1.df2dac2f2d47fp-45, -0x1.51b17f95fcc91p-43,
      0x1.76996ddc975d7p-42, -0x1.546155a7f71dap-41, 0x1.0456ed89c4f25p-40, -0x1.55d62c910e918p-40,
      0x1.86ead99977305p-40, -0x1.89aba61a64c46p-40, 0x1.6011e175fb0fcp-40}},
    {0x1.0000000000000p+0, -0x1.a6d7d18831888p-55,
     {0x1.3e296303b2297p-51, -0x1.d8456ef97c759p-49, 0x1.ccb92e6c24c8dp-47, -0x1.4c1aa8cf10b54p-45,
      0x1.7918b6b83c0fbp-44, -0x1.5f07365cc71f3p-43, 0x1.134d070b5921ep-42, -0x1.730a22eae78b0p-42,
      0x1.b4091041f5829p-42, -0x1.c3d456b2c9ab3p-42, 0x1.a06b4f4c53482p-42}},
};
// clang-format on

// The intervals of the float form above 1/2: interval i covers
// [i/16, (i + 1)/16) for i from kErfFloatFirst, and the last one holds
// kErfFloatOneFrom.  A row holds the coefficients c0 to c7 of the minimax
// polynomial of degree 7 in the offset from the center of the interval, for
// the relative error of erf.  A row is 64 bytes, one cache line, so the
// address of a row is a shift.
struct alignas(64) ErfFloatInterval {
    double terms[8] = {};
};

inline constexpr int kErfFloatFirst = 8;
inline constexpr int kErfFloatIntervals = 55;

// From python3 utils/tools/fp_tables.py erff.  The largest relative error of
// the table polynomials is 2^-52.40.
// clang-format off
inline constexpr ErfFloatInterval kErfFloatTable[kErfFloatIntervals] = {
    {{0x1.1855a5fd3dd50p-1, 0x1.b3aafcc275036p-1, -0x1.cee5ac8e92bbbp-2, -0x1.fa02983d0b575p-4, 0x1.77cd746cbd593p-3, -0x1.fa6e567a456e7p-10, -0x1.8de754d69659ep-5, 0x1.004de680295cap-7}},
    {{0x1.32a54cb8db67ap-1, 0x1.96164fafd8de9p-1, -0x1.e23a7ea0c9ad9p-2, -0x1.3f5ee156bab41p-4, 0x1.70e468a3df2c4p-3, -0x1.3da6771b3d5e4p-6, -0x1.69ed9bf74d6f1p-5, 0x1.8c81eb24b0dc7p-7}},
    {{0x1.4b13713ad3513p-1, 0x1.7791b886e7408p-1, -0x1.ecef423109bf9p-2, -0x1.15c3c5cf22420p-5, 0x1.5f688fc93778dp-3, -0x1.1da63bf3a9cf4p-5, -0x1.38427ce39995ep-5, 0x1.f9e6af5e7c00ep-7}},
    {{0x1.61955607dd15dp-1, 0x1.58a445da7c750p-1, -0x1.ef6c246a0f670p-2, 0x1.e83e0d9be210ep-8, 0x1.44cc6553609f7p-3, -0x1.87d3c306de4c9p-5, -0x1.f90b10b7282d4p-6, 0x1.222280c4829a1p-6}},
    {{0x1.762870f720c6fp-1, 0x1.39ccc1b136d5cp-1, -0x1.ea4feea4e4747p-2, 0x1.715e5952dd8e1p-5, 0x1.22cdbd83cbcd5p-3, -0x1.da50a9aa19761p-5, -0x1.754dc1016287cp-6, 0x1.35095bea596c4p-6}},
    {{0x1.88d1cd474a2e0p-1, 0x1.1b7e98fe26219p-1, -0x1.de65a22ce141cp-2, 0x1.40686a3f1ad84p-4, 0x1.f6b0cbb21f61fp-4, -0x1.09c7c928b8581p-4, -0x1.da752ab749c77p-7, 0x1.362bca9a65916p-6}},
    {{0x1.999d4192a5715p-1, 0x1.fc3ee5d1524b1p-2, -0x1.cc990045b55cbp-2, 0x1.b37338e6974e6p-4, 0x1.a0d120c87b9e2p-4, -0x1.19bb2b68a38f2p-4, -0x1.a110f768babebp-8, 0x1.272f89ee3f286p-6}},
    {{0x1.a89c850b7d54dp-1, 0x1.c40b0729ed548p-2, -0x1.b5eaaef0a234ap-2, 0x1.0847c7dad3ad2p-3, 0x1.47de0ba6da1d7p-4, -0x1.1d9de7fa3faf3p-4, 0x1.30ffb8a460c35p-10, 0x1.0aa14b8b1612cp-6}},
    {{0x1.b5e62fce16096p-1, 0x1.8eed36b886d93p-2, -0x1.9b64a06e50708p-2, 0x1.2bb6e2c74d725p-3, 0x1.dee3261cb6278p-5, -0x1.1699608e26cd9p-4, 0x1.fdff36650616cp-8, 0x1.c759d4b791ec5p-7}},
    {{0x1.c194b1d49a184p-1, 0x1.5d4fd33729013p-2, -0x1.7e0f4f045addep-2, 0x1.444bc66c39d01p-3, 0x1.356dbf16fad41p-5, -0x1.0643de8bef042p-4, 0x1.b281aee4128f0p-7, 0x1.6ba144867f56dp-7}},
    {{0x1.cbc54b476248ep-1, 0x1.2f7cc3fe6f422p-2, -0x1.5ee8429e36debp-2, 0x1.52a8395f9d49cp-3, 0x1.313761ba3ca4ap-6, -0x1.dcf845bc4a914p-5, 0x1.1e1420b68ceb6p-6, 0x1.0924b75967eb8p-7}},
    {{0x1.d4970f9ce00d9p-1, 0x1.059f59af7a904p-2, -0x1.3eda354de36c5p-2, 0x1.57b85ad43f626p-3, 0x1.8e913b98ae61bp-10, -0x1.a2893c9098611p-5, 0x1.4d3a90afe38a8p-6, 0x1.4cf57cbcc3375p-8}},
    {{0x1.dc29fb60715b0p-1, 0x1.bf8e1b1ca2274p-3, -0x1.1eb7095e5d6d4p-2, 0x1.549ea6f7aae08p-3, -0x1.b10f12f3690f7p-7, -0x1.61420d219a196p-5, 0x1.674f1f6a4e348p-6, 0x1.25692125e7578p-9}},
    {{0x1.e29e22a89d767p-1, 0x1.7bd5c7df3fe98p-3, -0x1.fe674494077c1p-3, 0x1.4a9feacf89a68p-3, -0x1.a00826906bd1cp-6, -0x1.1cf0e96533820p-5, 0x1.6e0d2ed4e4c40p-6, -0x1.35f8fdcecf1c1p-12}},
    {{0x1.e812fc64db36ap-1, 0x1.3fda6bc016990p-3, -0x1.c1cb278627921p-3, 0x1.3b105123172e2p-3, -0x1.1e6457bb18568p-5, -0x1.b1f647dd36106p-6, 0x1.640a533498b7fp-6, -0x1.3da42214aab46p-9}},
    {{0x1.eca6ccd709544p-1, 0x1.0b3f52ce8c37fp-3, -0x1.8885019f63c6dp-3, 0x1.274275fc9302fp-3, -0x1.57f73699a6317p-5, -0x1.3076a35ea03c3p-6, 0x1.4c6ae03d8e78fp-6, -0x1.0be2d29b1439cp-8}},
    {{0x1.f0762fde45ee7p-1, 0x1.bb1c972f23e49p-4, -0x1.5341e3c01b58ep-3, 0x1.107929f6f1776p-3, -0x1.7e1b34f97615fp-5, -0x1.73b625ea7412ap-7, 0x1.2a97ee14f06a3p-6, -0x1.595d869b02871p-8}},
    {{0x1.f39bc242e43e6p-1, 0x1.6c7e64e7281c5p-4, -0x1.2274b86835fd3p-3, 0x1.efb890e5c8167p-4, -0x1.92c7db1684723p-5, -0x1.45477e08b86fcp-8, 0x1.01fc6165c66b9p-6, -0x1.8844932fcdbb2p-8}},
    {{0x1.f62fe80272419p-1, 0x1.297db960e4f5dp-4, -0x1.ecb83b087c04fp-4, 0x1.bce18363ca57fp-4, -0x1.985aaf7764f4dp-5, 0x1.cd953e2693954p-12, 0x1.ab9a0b8e75102p-7, -0x1.9b5e38aab46b3p-8}},
    {{0x1.f848acb544e95p-1, 0x1.e1d4cf1e24501p-5, -0x1.9e12e1fde5552p-4, 0x1.8a27806df39f9p-4, -0x1.91674e5eb3d78p-5, 0x1.3bc755aec4dadp-8, 0x1.51bc5381b0213p-7, -0x1.96b274708e7a1p-8}},
    {{0x1.f9f9ba8d3c733p-1, 0x1.83298d7172109p-5, -0x1.58d101f905a75p-4, 0x1.58f1456f85e53p-4, -0x1.808d1850b8cadp-5, 0x1.0c1bd9b153a93p-7, 0x1.f61e9d89c3516p-8, -0x1.7f0821c456354p-8}},
    {{0x1.fb54641aebbc9p-1, 0x1.34ac36ad8dafap-5, -0x1.1c8ec267f9404p-4, 0x1.2a52c5d84128bp-4, -0x1.68541c02b4536p-5, 0x1.5afe401880918p-7, 0x1.565b2d7be715dp-8, -0x1.596b18a03d38bp-8}},
    {{0x1.fc67bcf2d7b8fp-1, 0x1.e85c449e377efp-6, -0x1.d177f166c07c5p-5, 0x1.fe23b7584aaa4p-5, -0x1.4b12109613c70p-5, 0x1.8d9905d570c55p-7, 0x1.92650342ca745p-9, -0x1.2ac505d9088a2p-8}},
    {{0x1.fd40bd6d7a785p-1, 0x1.7f5188610ddc8p-6, -0x1.7954423f7c998p-5, 0x1.af5baae337febp-5, -0x1.2ad77c7cbc950p-5, 0x1.a7b8c48fee6ecp-7, 0x1.46646efaef56cp-10, -0x1.ef1a75ec237cep-9}},
    {{0x1.fdea6e062d0c9p-1, 0x1.2a875b5ffab59p-6, -0x1.2f3178cd6dcd4p-5, 0x1.68d1c45b93addp-5, -0x1.09648ed3aef12p-5, 0x1.ad8b151a7bd7ap-7, -0x1.e9a6019024109p-13, -0x1.87234ac4bcbc3p-9}},
    {{0x1.fe6e1742f7cf5p-1, 0x1.cd5ec93c1243ap-7, -0x1.e2ff3aaacb385p-6, 0x1.2aa4e5823c7bdp-5, -0x1.d049842dbebdcp-6, 0x1.a34edb2b3ffb7p-7, -0x1.676e5981ce79fp-10, -0x1.23b071b82a18ap-9}},
    {{0x1.fed37386190fbp-1, 0x1.61beae53b72c2p-7, -0x1.7d6193f22c3c0p-6, 0x1.e947279e3b4fep-6, -0x1.906031b97d09ep-6, 0x1.8d14d62be198ep-7, -0x1.1f245e69b7086p-9, -0x1.92584bd3ea7acp-10}},
    {{0x1.ff20e0a7ba8c2p-1, 0x1.0d1d69569b839p-7, -0x1.2a8ca0dc02752p-6, 0x1.8cc071b70932ap-6, -0x1.54a149f1b09fep-6, 0x1.6e9137b55ffaap-7, -0x1.6577ed39b6358p-9, -0x1.e9c23e0d72b8ep-11}},
    {{0x1.ff5b8fb26f5f6p-1, 0x1.9646f35a7663cp-8, -0x1.cf68ed9311b0bp-7, 0x1.3e8735b5a66cbp-6, -0x1.1e1612d027073p-6, 0x1.4afd8e6f2f18bp-7, -0x1.8c375170091e1p-9, -0x1.c799fe7378d7ap-12}},
    {{0x1.ff87b1913e853p-1, 0x1.30499b5039596p-8, -0x1.64964201ec8bap-7, 0x1.fa73d7eafb79dp-7, -0x1.daa30221421e3p-7, 0x1.2509444d81e40p-7, -0x1.99482a2e34ffep-9, -0x1.40408213eb8b3p-15}},
    {{0x1.ffa89fe5b3625p-1, 0x1.c4412bf4b8f35p-9, -0x1.100f347126cf0p-7, 0x1.8ebda07671ba8p-7, -0x1.850c6a31c9b9fp-7, 0x1.fdac860dfdc26p-8, -0x1.927d03d0f0764p-9, 0x1.0ff5e6f4335d5p-12}},
    {{0x1.ffc10194fcb64p-1, 0x1.4d78bba8ca621p-9, -0x1.9ba107a443e02p-8, 0x1.36f273fbc040cp-7, -0x1.3b38716ac7d24p-7, 0x1.b3fe0182535c5p-8, -0x1.7d3fe7df6ab05p-9, 0x1.ea31dd16593a0p-12}},
    {{0x1.ffd2eae369a07p-1, 0x1.e7f232d9e266cp-10, -0x1.34c7442dd48d9p-8, 0x1.e066bed070961p-8, -0x1.f914f3c42fb4ap-8, 0x1.6f4664ed78371p-8, -0x1.5e5991079ffc1p-9, 0x1.39cbb0c0b3419p-11}},
    {{0x1.ffdff92db56e5p-1, 0x1.6235fbd7a4373p-10, -0x1.cb5e029b9e56ap-9, 0x1.6fa4c7ef274a9p-8, -0x1.903a089a835b7p-8, 0x1.30f12e0cba5a5p-8, -0x1.39d21b6970774p-9, 0x1.5d3f82cd2cd5bp-11}},
    {{0x1.ffe96a78a04a9p-1, 0x1.fe41cd9bb4f2cp-11, -0x1.52d7b28966c0cp-9, 0x1.16c192d86a1a9p-8, -0x1.39bfce9510ffdp-8, 0x1.f376a7869d54bp-9, -0x1.12e6cef99f9b5p-9, 0x1.66acd4eb82984p-11}},
    {{0x1.fff0312b010b5p-1, 0x1.6caa0d3583018p-11, -0x1.efb729f4cf75bp-10, 0x1.a2da7cebe12cbp-9, -0x1.e6c27a24bc725p-9, 0x1.93b1f4d8da09cp-9, -0x1.d82050aa9819ap-10, 0x1.5cd7dd10b09adp-11}},
    {{0x1.fff50456dab8cp-1, 0x1.0295ef6591865p-11, -0x1.679880e95a4dap-10, 0x1.37d38e3a5c918p-9, -0x1.75b3708aebb36p-9, 0x1.4231c4b4988fep-9, -0x1.8e264764a01fcp-10, 0x1.45c3b64cccad5p-11}},
    {{0x1.fff86cfd3e657p-1, 0x1.6be02102b353dp-12, -0x1.02b157780d6afp-10, 0x1.cc1d886861169p-10, -0x1.1bff6f12ec8cfp-9, 0x1.fc0f77bd80e83p-10, -0x1.4a3320bd3ebcap-10, 0x1.267f8bd0cd633p-11}},
    {{0x1.fffad0b901755p-1, 0x1.fc0d55470cf5ep-13, -0x1.7121aff5e820dp-11, 0x1.506d6992f7e01p-10, -0x1.ab595d3ecd67ap-10, 0x1.8bdd79da9d457p-10, -0x1.0d9b090eab08ep-10, 0x1.031aba5abee5ap-11}},
    {{0x1.fffc7a37857d2p-1, 0x1.5feada379d8a5p-13, -0x1.05304df58f3a9p-11, 0x1.e79c081b860bcp-11, -0x1.3e5dbe33237f4p-10, 0x1.30eb2081d9c33p-10, -0x1.b1d493af919dcp-11, 0x1.bd587d04927b4p-12}},
    {{0x1.fffd9fdeabccep-1, 0x1.e3bcf436a1a48p-14, -0x1.6e953111ef0a1p-12, 0x1.5e3edf67686f7p-11, -0x1.d5be67c0547bep-11, 0x1.d07d9ff9d2736p-11, -0x1.58328f5efe2a9p-11, 0x1.76d42ed4655dbp-12}},
    {{0x1.fffe68f4fa777p-1, 0x1.49e17724f4cddp-14, -0x1.fe48c44e229c3p-13, 0x1.f2bd95d76f24ap-12, -0x1.57388cb12cda0p-11, 0x1.5decc25c2cadcp-11, -0x1.0d7499d1f4d55p-11, 0x1.35933399e4bd0p-12}},
    {{0x1.fffef1960d85dp-1, 0x1.be6abbb10a4cdp-15, -0x1.6040381a8c314p-13, 0x1.5fff1dde9eefep-12, -0x1.f0c933efa955dp-12, 0x1.04cbf4a5b31c6p-11, -0x1.a07f150b79321p-12, 0x1.f68dd27b39960p-13}},
    {{0x1.ffff4db27f146p-1, 0x1.2bb5cc22e5cd8p-15, -0x1.e25894899f526p-14, 0x1.ec8a8e5a72795p-13, -0x1.64256ae0a3bfcp-12, 0x1.80a836c1740b9p-12, -0x1.3dea401b0a235p-12, 0x1.915de07818839p-13}},
    {{0x1.ffff8b500e77cp-1, 0x1.8f4ccca7fc769p-16, -0x1.478cffe305945p-14, 0x1.559f04adde4efp-13, -0x1.f9e1577d69b28p-13, 0x1.18bda53c0e884p-12, -0x1.df8634c26e46ep-13, 0x1.3bb5c6ee34a81p-13}},
    {{0x1.ffffb43555b5fp-1, 0x1.07ebd2a2d26c8p-16, -0x1.b93e442a37f27p-15, 0x1.d5cf15159cda1p-14, -0x1.63f5e1469cb15p-13, 0x1.95a03acec4f0ap-13, -0x1.656e5e284af9dp-13, 0x1.e98c43833c7f2p-14}},
    {{0x1.ffffcf23ff5fcp-1, 0x1.5a2adfa0b492cp-17, -0x1.26c88270759ebp-15, 0x1.40473572b9932p-14, -0x1.f057cbde5909fp-14, 0x1.22178d1c49bc5p-13, -0x1.0765b6181aa76p-13, 0x1.765b3e9481e6ap-14}},
    {{0x1.ffffe0bd3e852p-1, 0x1.c282cd3957a73p-18, -0x1.86ad6dfa44fa5p-16, 0x1.b0f313f039f84p-15, -0x1.56e44abecdec7p-14, 0x1.9ad1ecfe4ea8ap-14, -0x1.7fe4033244255p-14, 0x1.1a8184c03c364p-14}},
    {{0x1.ffffec2641a9ep-1, 0x1.22df29821407dp-18, -0x1.00c902a6cfd95p-16, 0x1.22234eb886702p-15, -0x1.d57a181c9f4ddp-15, 0x1.200c283b5a4e4p-14, -0x1.14b4c32811562p-14, 0x1.a4f966edc6d1ep-15}},
    {{0x1.fffff37d63a36p-1, 0x1.74adc8f405eecp-19, -0x1.4ed4228e44856p-17, 0x1.81918baea931dp-16, -0x1.3e81b17a0091bp-15, 0x1.9004a3610941fp-15, -0x1.8aa1ba3e6d2d0p-15, 0x1.35cd4e3ab8296p-15}},
    {{0x1.fffff82cdcf1bp-1, 0x1.d9c73698fa87dp-20, -0x1.b11017ec67116p-18, 0x1.fc0dfadf65484p-17, -0x1.ac4e03cd2e0bep-16, 0x1.131806b5a4338p-15, -0x1.1672ef66b8fc9p-15, 0x1.c2882c9115099p-16}},
    {{0x1.fffffb248c39dp-1, 0x1.2acee2f5ec66ap-20, -0x1.15cc570408a39p-18, 0x1.4be757bbb75f9p-17, -0x1.1d6aa5f8d2273p-16, 0x1.76c5937d4ae9bp-16, -0x1.84dffc3d7db73p-16, 0x1.43c8315edb532p-16}},
    {{0x1.fffffd01f36afp-1, 0x1.75fa8dbc840bap-21, -0x1.6186da0133f62p-19, 0x1.ae023231e1b59p-18, -0x1.790812f7c910ep-17, 0x1.f9c25656cedf5p-17, -0x1.0cc666842f6f4p-16, 0x1.cc170a6171e9cp-17}},
    {{0x1.fffffe2ba0ea5p-1, 0x1.d06ad6ecde88ep-22, -0x1.be46aa8edc9b0p-20, 0x1.143860c7840f0p-18, -0x1.edaba78faee6bp-18, 0x1.52138a96f01e2p-17, -0x1.6fca538eea2f8p-17, 0x1.43404047a6425p-17}},
    {{0x1.fffffee3cc32cp-1, 0x1.1e1e857adb8ddp-22, -0x1.1769ce5f2a6f4p-20, 0x1.5fe5d479b057cp-19, -0x1.405d865c92e07p-18, 0x1.bfc94feba1942p-18, -0x1.f245d5f854bfcp-18, 0x1.c142452f9f491p-18}},
};
// clang-format on

inline namespace CRUCIBLE_SIMD_TARGET {

// erf(x) for |x| < 2^-28: 2/sqrt(pi) x with one round.  For a normal x, the
// scale 2^64 keeps the low part of the product normal, and the multiply by
// 2^-64 is exact, because the result is normal.  For a subnormal x, the fma
// rounds the product one time at the precision of its result.  The low part
// of 2/sqrt(pi) times x is then below 2^-1078, a sixteenth of the last place.
[[nodiscard]] constexpr double erf_tiny_(double value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    if (::foundation::core::fp::abs(value) < 0x1p-1022) {
        return fma_of_finite(kErfTwoOverRootPiHigh, value, kErfTwoOverRootPiLow * value);
    }
    double const scaled = value * 0x1p64;
    return fma_of_finite(kErfTwoOverRootPiHigh, scaled, kErfTwoOverRootPiLow * scaled) * 0x1p-64;
}

// erf(x) for |x| < 1/2: x (2/sqrt(pi) + z P(z)) with z = x^2, with the terms P0
// to P(Degree) of P.  The high part of 2/sqrt(pi) times x is exact inside the
// last fma, so the result rounds one time beside the small error of the tail.
template <int Degree>
[[nodiscard]] constexpr double erf_small_(double value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    double const square = value * value;
    double sum = kErfP[Degree];
    // Degree is a constant, so the loop unrolls into a chain of fma with no
    // counter and no branch.  The order of the fma is the order of the loop.
#pragma GCC unroll 16
    for (int term = Degree - 1; term >= 0; --term) {
        sum = fma_of_finite(square, sum, kErfP[term]);
    }
    double const tail = fma_of_finite(square, sum, kErfTwoOverRootPiLow);
    return fma_of_finite(kErfTwoOverRootPiHigh, value, value * tail);
}

// erf(|x|) for 1/2 <= |x| < 6, with the Taylor terms c1 to c(Degree) of the
// interval of |x|.
template <int Degree>
[[nodiscard]] constexpr double erf_interval_(double magnitude) noexcept {
    // magnitude - 1/2 is exact, its product with 8 is exact, and the bounded
    // value converts to an index of 0 to 43.
    auto const index = static_cast<std::uint32_t>((magnitude - 0.5) * 8.0);
    double const center = 0.5625 + 0.125 * static_cast<double>(index);
    double const offset = magnitude - center;
    ErfInterval const& entry = kErfTable[index];
    double sum = entry.terms[Degree - 1];
#pragma GCC unroll 16
    for (int term = Degree - 2; term >= 0; --term) {
        sum = ::fixy::fp::detail::fma_of_finite(offset, sum, entry.terms[term]);
    }
    return entry.value_high + ::fixy::fp::detail::fma_of_finite(offset, sum, entry.value_low);
}

// floor(t) for 0 <= t < 2^52, exact.  With SSE4.1 and on aarch64 it is the
// round instruction.  At x86-64 the add of 2^52 rounds t to an integer, and
// the compare takes one off when that round went up.
[[nodiscard]] constexpr double erf_floor_(double value) noexcept {
#if defined(__SSE4_1__) || defined(__aarch64__)
    return __builtin_floor(value);
#else
    double const rounded = (value + 0x1p52) - 0x1p52;
    return rounded > value ? rounded - 1.0 : rounded;
#endif
}

// c0 + c1 u + ... + c7 u^7 of the first eight terms, in the form of Estrin:
// four fma of pairs, two of pairs of pairs, and one with u^4.  The chain is
// three fma deep after u^4, where the form of Horner is seven fma deep.
template <std::size_t Count>
[[nodiscard]] constexpr double erf_estrin_eight_(double point, double const (&terms)[Count]) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    static_assert(Count >= 8, "the form of Estrin reads eight terms");
    double const square = point * point;
    double const fourth_power = square * square;
    double const first = fma_of_finite(point, terms[1], terms[0]);
    double const second = fma_of_finite(point, terms[3], terms[2]);
    double const third = fma_of_finite(point, terms[5], terms[4]);
    double const fourth = fma_of_finite(point, terms[7], terms[6]);
    double const low = fma_of_finite(square, second, first);
    double const high = fma_of_finite(square, fourth, third);
    return fma_of_finite(fourth_power, high, low);
}

// erf of float below 1/2: x Q(z) with z = x^2, and Q of kErfFloatQ in the form
// of Estrin.  The multiply by x is the last round of the double result.
[[nodiscard]] constexpr double erf_small_float_(double value) noexcept {
    return value * erf_estrin_eight_(value * value, kErfFloatQ);
}

// The fast path of erf of float for 1/2 <= |x| < kErfFloatOneFrom: the
// polynomial of the row of kErfFloatTable in the form of Estrin, with no add
// after it.  The index comes from the float: 16 |x| is exact, and its
// truncation is its floor.  The offset comes from the double:
// (|x| - 1/32) - floor(16 |x|) / 16 is exact, and it is |x| minus the center
// of the interval.  The two chains do not wait for each other.
[[nodiscard]] constexpr double erf_interval_float_(double magnitude, float magnitude_float) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    auto const index = static_cast<std::uint32_t>(16.0f * magnitude_float) - static_cast<std::uint32_t>(kErfFloatFirst);
    double const offset = fma_of_finite(-0.0625, erf_floor_(16.0 * magnitude), magnitude - 0.03125);
    return erf_estrin_eight_(offset, kErfFloatTable[index].terms);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr double erf(double value) noexcept {
    std::uint64_t const magnitude_bits = __builtin_bit_cast(std::uint64_t, value) & 0x7fffffffffffffffULL;
    double const magnitude = __builtin_bit_cast(double, magnitude_bits);
    // One quiet compare holds the edge to 1, the infinities and the NaNs, as
    // in erf(float).  It is true for a NaN, so after it the compiler knows that
    // the magnitude is in [0, kErfOneFrom).  Then the conversion of the index
    // in erf_interval_ needs no check of its range under the sanitizer of the
    // conversions.
    if (!__builtin_isless(magnitude, detail::kErfOneFrom)) [[unlikely]] {
        if (magnitude_bits > 0x7ff0000000000000ULL) [[unlikely]]
            return ::foundation::core::quiet_nan_of<double>;
        return ::foundation::core::fp::copy_sign(1.0, value);
    }
    if (magnitude < 0.5) {
        if (magnitude < 0x1p-28) return detail::erf_tiny_(value);
        return detail::erf_small_<detail::kErfSmallDegree>(value);
    }
    return ::foundation::core::fp::copy_sign(detail::erf_interval_<detail::kErfDegree>(magnitude), value);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr float erf(float value) noexcept {
    std::uint32_t const magnitude_bits = __builtin_bit_cast(std::uint32_t, value) & 0x7fffffffu;
    // The and of the sign on the float register keeps the value off the
    // integer registers, so the chain of the result has no move there and
    // back.  The tests read the bits.
    float const magnitude = ::foundation::core::fp::abs(value);
    // One quiet compare holds the edge to 1, the infinities and the NaNs, and
    // it raises no flag for a quiet NaN.  It is true for a NaN, so after it the
    // compiler knows that the magnitude is in [0, kErfFloatOneFrom).  Then the
    // conversion of the index in erf_interval_float_ needs no check of its
    // range under the sanitizer of the conversions.
    if (!__builtin_isless(magnitude, detail::kErfFloatOneFrom)) [[unlikely]] {
        if (magnitude_bits > 0x7f800000u) [[unlikely]]
            return ::foundation::core::quiet_nan_of<float>;
        return ::foundation::core::fp::copy_sign(1.0f, value);
    }
    // The round of the double result is the correctly rounded float for each
    // input, a subnormal result too.  test_fp_erf_float_exhaustive proves it.
    double const input = static_cast<double>(magnitude);
    double const result =
        magnitude < 0.5f ? detail::erf_small_float_(input) : detail::erf_interval_float_(input, magnitude);
    return ::foundation::core::fp::copy_sign(static_cast<float>(result), value);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp
