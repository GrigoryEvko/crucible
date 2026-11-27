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
// floating-point unit on purpose.
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
//   The fast path is the double path with fewer terms: P cut to degree 7
//   below 1/2, and the Taylor polynomials cut to degree 8.  A cut of a Taylor
//   polynomial is the Taylor polynomial of the lower degree, with an error of
//   about 2^-44 relative to erf.  A cut of the minimax P drops the terms of
//   P8 and P9, which give at most 2^-40.7 relative to erf.  The rounding test
//   admits an error of 2^-38.  |x| >= 0x1.f5a88ap+1 gives ±1.  The rounding
//   test and the slow path are those of exp(float) in fixy/fp/Exp.h, with
//   erf(double) in the slow path, and each result below 2^-123 takes the slow
//   path.  The fma sites of the fast path are those of the double path.
//   The exhaustive sweep of the 2^32 inputs against CORE-MATH proves the
//   correct round of each input (the target bench_fp_sweep).
//
// Tables
//   `python3 utils/tools/fp_tables.py erf` prints each table and constant of
//   this header with the pinned mpmath.

#include <foundation/Platform.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <fixy/fp/FmaOfFinite.h>
#include <fixy/fp/RoundToFloat.h>

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
// The degree of P below 1/2, and the degrees of the fast path of the float
// form, in the intervals and in z.
inline constexpr int kErfSmallDegree = 9;
inline constexpr int kErfFloatDegree = 8;
inline constexpr int kErfFloatSmallDegree = 7;

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

// The tolerance of the rounding test of the float fast path, in units of the
// last bit of the double result: an error of 2^-38 times 2^53.  The error of
// the fast path is at most about 2^-40.7, from the cut of P, so the margin is
// about six.
inline constexpr std::uint64_t kErfFloatTolerance = std::uint64_t{1} << 15;

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
    for (int term = Degree - 2; term >= 0; --term) {
        sum = ::fixy::fp::detail::fma_of_finite(offset, sum, entry.terms[term]);
    }
    return entry.value_high + ::fixy::fp::detail::fma_of_finite(offset, sum, entry.value_low);
}

// The midpoint inputs of the float form, from the sweep of the oracle.  A
// midpoint input that is not in the list takes the round to even.
[[gnu::cold, nodiscard]] constexpr float erf_float_midpoint_(std::uint32_t input_bits, double result) noexcept {
    switch (input_bits) {
        default:
            return static_cast<float>(result);
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr double erf(double value) noexcept {
    std::uint64_t const magnitude_bits = __builtin_bit_cast(std::uint64_t, value) & 0x7fffffffffffffffULL;
    double const magnitude = __builtin_bit_cast(double, magnitude_bits);
    if (magnitude_bits >= 0x7ff0000000000000ULL) [[unlikely]] {
        if (magnitude_bits > 0x7ff0000000000000ULL) return ::foundation::core::quiet_nan_of<double>;
        return ::foundation::core::fp::copy_sign(1.0, value);
    }
    if (magnitude < 0.5) {
        if (magnitude < 0x1p-28) return detail::erf_tiny_(value);
        return detail::erf_small_<detail::kErfSmallDegree>(value);
    }
    if (magnitude >= detail::kErfOneFrom) return ::foundation::core::fp::copy_sign(1.0, value);
    return ::foundation::core::fp::copy_sign(detail::erf_interval_<detail::kErfDegree>(magnitude), value);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp

namespace fixy::fp::detail {

inline namespace CRUCIBLE_SIMD_TARGET {

// The slow path of the float form: the round of erf(double), with the list of
// the midpoint inputs.
[[gnu::cold, gnu::noinline, nodiscard]] constexpr float erf_float_slow_(float value) noexcept {
    double const result = ::fixy::fp::erf(static_cast<double>(value));
    if (is_float_midpoint_(result)) [[unlikely]] {
        return erf_float_midpoint_(__builtin_bit_cast(std::uint32_t, value), result);
    }
    return static_cast<float>(result);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr float erf(float value) noexcept {
    std::uint32_t const magnitude_bits = __builtin_bit_cast(std::uint32_t, value) & 0x7fffffffu;
    if (magnitude_bits >= 0x7f800000u) [[unlikely]] {
        if (magnitude_bits > 0x7f800000u) return ::foundation::core::quiet_nan_of<float>;
        return ::foundation::core::fp::copy_sign(1.0f, value);
    }
    float const magnitude = __builtin_bit_cast(float, magnitude_bits);
    if (magnitude >= detail::kErfFloatOneFrom) return ::foundation::core::fp::copy_sign(1.0f, value);
    // A result below 2^-123 takes the slow path: the rounding test reads 24
    // bits of precision, and a subnormal float has fewer.
    if (magnitude_bits < 0x02000000u) [[unlikely]]
        return detail::erf_float_slow_(value);
    double const input = static_cast<double>(magnitude);
    double const result = magnitude < 0.5f ? detail::erf_small_<detail::kErfFloatSmallDegree>(input)
                                           : detail::erf_interval_<detail::kErfFloatDegree>(input);
    if (detail::is_near_float_midpoint_(result, detail::kErfFloatTolerance)) [[unlikely]] {
        return detail::erf_float_slow_(value);
    }
    return ::foundation::core::fp::copy_sign(static_cast<float>(result), value);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp
