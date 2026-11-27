#pragma once

// The hard inputs of log(double), with the correctly rounded result of the
// pinned CORE-MATH (cr_log of utils/pins.txt), for test_fp_log_double and the
// digests of fp_elementary_forms.h.
//
// The list has two parts:
//   - the four hardest inputs of the list log.wc of CORE-MATH in each range:
//     the subnormal inputs, the path near 1, e = 0 (x in [sqrt2/2, sqrt2)),
//     |e| above 1000, and the inputs above 1 and below 1 of the block of the
//     list of Lefevre and of the block of the BaCSeL search at -m 48.  The
//     comment gives the distance to the nearest midpoint in units of the last
//     place.  The first input of the block of Lefevre above 1 is the hardest
//     input of the whole list.
//   - the edges of the paths: 1, the neighbors of 1, the two edges of the
//     path near 1, the edges of k2 = 0 of the slow path, the edges of the
//     reduction at sqrt2 and sqrt2/2, the smallest and the largest subnormal
//     input, the smallest normal input, the largest finite input, 2, 1/2 and
//     the double nearest e.

#include <fixy/Core.h>

#include <cstdint>

namespace test_fp_log {

// One input and its correctly rounded result, as bits.
struct HardCase {
    std::uint64_t input = 0;
    std::uint64_t expected = 0;
};

inline constexpr ::fixy::FixedArray<HardCase, 54> kLogHardCases{
    // The hardest inputs of each range of the list log.wc.
    HardCase{0x0001104D89F02A00ULL, 0xC08638DBB037825FULL},  // 0x0.1104d89f02ap-1022, subnormal, 2^-55.53
    HardCase{0x00019B99592DFC98ULL, 0xC086358D96B1CB2DULL},  // 0x0.19b99592dfc98p-1022, subnormal, 2^-52.96
    HardCase{0x00000B7751DFAAFAULL, 0xC0865232C555D8F8ULL},  // 0x0.00b7751dfaafap-1022, subnormal, 2^-52.48
    HardCase{0x0001549018654431ULL, 0xC086371194FBA02EULL},  // 0x0.1549018654431p-1022, subnormal, 2^-50.94
    HardCase{0x3FEFFFFFFFFFFFFEULL, 0xBCB0000000000001ULL},  // 0x1.ffffffffffffep-1, near 1, 2^-53.58
    HardCase{0x3FEFF78281F6DAF5ULL, 0xBF50FD3D2385BF25ULL},  // 0x1.ff78281f6daf5p-1, near 1, 2^-46.16
    HardCase{0x3FEFF46958C4B10EULL, 0xBF573181D2809277ULL},  // 0x1.ff46958c4b10ep-1, near 1, 2^-44.75
    HardCase{0x3FEFFF685970E9B3ULL, 0xBF12F4FECE4C20B9ULL},  // 0x1.fff685970e9b3p-1, near 1, 2^-44.01
    HardCase{0x3FEC7E1077F9AEC2ULL, 0xBFBDB88B1160524BULL},  // 0x1.c7e1077f9aec2p-1, e = 0, 2^-52.19
    HardCase{0x3FEFE5755F3A16E2ULL, 0xBF6A95A8AEB6BBC4ULL},  // 0x1.fe5755f3a16e2p-1, e = 0, 2^-52.10
    HardCase{0x3FF3F04A8EB275E6ULL, 0x3FCC2B46FBD7EAF0ULL},  // 0x1.3f04a8eb275e6p+0, e = 0, 2^-50.82
    HardCase{0x3FEA4970C846CF08ULL, 0xBFC92C5A71371384ULL},  // 0x1.a4970c846cf08p-1, e = 0, 2^-50.79
    HardCase{0x7F4FCDB4D6DB2715ULL, 0x4085F6C2BAC6A672ULL},  // 0x1.fcdb4d6db2715p+1013, |e| > 1000, 2^-58.77
    HardCase{0x0099B45DA8E653FBULL, 0xC085F3046D51161AULL},  // 0x1.9b45da8e653fbp-1014, |e| > 1000, 2^-57.38
    HardCase{0x007FFE13EE43D9A6ULL, 0xC085FC5B6344A7F1ULL},  // 0x1.ffe13ee43d9a6p-1016, |e| > 1000, 2^-57.19
    HardCase{0x7F4BAF53E69C4CE2ULL, 0x4085F5A6B0868639ULL},  // 0x1.baf53e69c4ce2p+1013, |e| > 1000, 2^-56.25
    HardCase{0x6DBFD15DAA6CE332ULL, 0x407FC12387D0632AULL},  // 0x1.fd15daa6ce332p+732, Lefevre, 2^-62.03
    HardCase{0x705BE87838F1A47CULL, 0x4080C86AFFA8AF55ULL},  // 0x1.be87838f1a47cp+774, Lefevre, 2^-61.37
    HardCase{0x553B7F71A488641AULL, 0x406D86C518CEAB6BULL},  // 0x1.b7f71a488641ap+340, Lefevre, 2^-61.16
    HardCase{0x61E6B3D29C0F9E6EULL, 0x40778BA92CB32390ULL},  // 0x1.6b3d29c0f9e6ep+543, Lefevre, 2^-61.06
    HardCase{0x27F9476E304CD7C7ULL, 0xC0709B60CAF47B36ULL},  // 0x1.9476e304cd7c7p-384, Lefevre, 2^-61.57
    HardCase{0x3DC613955DC802F8ULL, 0xC037F02F9BAF6035ULL},  // 0x1.613955dc802f8p-35, Lefevre, 2^-61.37
    HardCase{0x0D0A8346ABF875C7ULL, 0xC081A34780A6CD60ULL},  // 0x1.a8346abf875c7p-815, Lefevre, 2^-60.84
    HardCase{0x1C191F8729A9282BULL, 0xC078D6A4FD0BD952ULL},  // 0x1.91f8729a9282bp-574, Lefevre, 2^-60.76
    HardCase{0x6E3DFDF9DFD2ABDDULL, 0x40800C75440D8562ULL},  // 0x1.dfdf9dfd2abddp+740, -m 48, 2^-56.86
    HardCase{0x757A9CB8DC365815ULL, 0x40828EBDFCDA45BFULL},  // 0x1.a9cb8dc365815p+856, -m 48, 2^-56.72
    HardCase{0x6F8FDA768E5B84BFULL, 0x4080816372DB74ABULL},  // 0x1.fda768e5b84bfp+761, -m 48, 2^-56.65
    HardCase{0x72D910AEB868A927ULL, 0x4081A55D95831611ULL},  // 0x1.910aeb868a927p+814, -m 48, 2^-56.56
    HardCase{0x07BEB7B7D4CF0A1DULL, 0xC083797113F68321ULL},  // 0x1.eb7b7d4cf0a1dp-900, -m 48, 2^-56.89
    HardCase{0x08BFA3D048DF6B89ULL, 0xC083207B72BEAE96ULL},  // 0x1.fa3d048df6b89p-884, -m 48, 2^-56.65
    HardCase{0x0E3DCF97546624B6ULL, 0xC08138FBA366489AULL},  // 0x1.dcf97546624b6p-796, -m 48, 2^-56.46
    HardCase{0x099FE8C1B2B4E40FULL, 0xC082D2C82C9273AAULL},  // 0x1.fe8c1b2b4e40fp-870, -m 48, 2^-56.39
    // The edges of the paths.
    HardCase{0x3FF0000000000000ULL, 0x0000000000000000ULL},  // 1
    HardCase{0x3FEFFFFFFFFFFFFFULL, 0xBCA0000000000000ULL},  // below 1
    HardCase{0x3FF0000000000001ULL, 0x3CAFFFFFFFFFFFFFULL},  // above 1
    HardCase{0x3FEFF00000000000ULL, 0xBF60040155D5889EULL},  // 1 - 2^-9, the first input of the path near 1
    HardCase{0x3FEFEFFFFFFFFFFFULL, 0xBF60040155D5899EULL},  // below 1 - 2^-9, the fast path
    HardCase{0x3FF007FFFFFFFFFFULL, 0x3F5FF802A9AB0CE8ULL},  // below 1 + 2^-9, the last input of the path near 1
    HardCase{0x3FF0080000000000ULL, 0x3F5FF802A9AB10E6ULL},  // 1 + 2^-9, the fast path
    HardCase{0x3FF0000800000000ULL, 0x3EDFFFF80002AAAAULL},  // 1 + 2^-17: z 2^16 = 1/2 rounds to k2 = 0
    HardCase{0x3FF0000800000001ULL, 0x3EDFFFF80006AAA8ULL},  // above 1 + 2^-17: k2 = 1
    HardCase{0x3FEFFFF000000000ULL, 0xBEE0000400015556ULL},  // 1 - 2^-17: z 2^16 = -1/2 rounds to k2 = 0
    HardCase{0x3FEFFFEFFFFFFFFFULL, 0xBEE0000400025556ULL},  // below 1 - 2^-17: k2 = -1
    HardCase{0x3FF6A09E667F3BCCULL, 0x3FD62E42FEFA39EEULL},  // below sqrt2: e = 0, i = 724
    HardCase{0x3FF6A09E667F3BCDULL, 0x3FD62E42FEFA39F0ULL},  // above sqrt2: e = 1, i = 362
    HardCase{0x3FE6A09E667F3BCCULL, 0xBFD62E42FEFA39F1ULL},  // below sqrt2/2: e = -1, i = 724
    HardCase{0x3FE6A09E667F3BCDULL, 0xBFD62E42FEFA39EEULL},  // above sqrt2/2: e = 0, i = 362
    HardCase{0x0000000000000001ULL, 0xC0874385446D71C3ULL},  // the smallest subnormal input
    HardCase{0x000FFFFFFFFFFFFFULL, 0xC086232BDD7ABCD2ULL},  // the largest subnormal input
    HardCase{0x0010000000000000ULL, 0xC086232BDD7ABCD2ULL},  // the smallest normal input
    HardCase{0x7FEFFFFFFFFFFFFFULL, 0x40862E42FEFA39EFULL},  // the largest finite input
    HardCase{0x4000000000000000ULL, 0x3FE62E42FEFA39EFULL},  // 2
    HardCase{0x3FE0000000000000ULL, 0xBFE62E42FEFA39EFULL},  // 1/2
    HardCase{0x4005BF0A8B145769ULL, 0x3FF0000000000000ULL},  // the double nearest e
};

}  // namespace test_fp_log
