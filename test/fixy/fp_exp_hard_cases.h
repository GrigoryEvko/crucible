#pragma once

// The hard inputs of exp(double), with the correctly rounded result of the
// pinned CORE-MATH (cr_exp of utils/pins.txt), for test_fp_exp_double and the
// digests of fp_elementary_forms.h.
//
// The list has three parts:
//   - the 21 inputs of the list db of the exp of CORE-MATH, the inputs where
//     the accurate path of CORE-MATH needs an answer from a table
//   - the hardest input of each range of the coverage table of
//     fixy/fp/Exp.h: the hardest input of the list exp.wc of CORE-MATH, the
//     hardest input of the own check below 2^-30, the inputs next to 2^-53 and
//     -2^-54 that the list does not hold, the hardest inputs of the binade
//     [-2^-11, -2^-12), the hardest subnormal results, and the hardest results
//     in [2^-1022, 2^-1021)
//   - the edges of the paths: +inf, +0, the smallest subnormal result, the
//     smallest normal result, |x| = 512, and |x| = 2^-54.

#include <fixy/Core.h>

#include <cstdint>

namespace test_fp_exp {

// One input and its correctly rounded result, as bits.
struct HardCase {
    std::uint64_t input = 0;
    std::uint64_t expected = 0;
};

inline constexpr ::fixy::FixedArray<HardCase, 51> kExpHardCases{
    // The list db of CORE-MATH.
    HardCase{0xC038F80E06F3A04CULL, 0x3DAF80AAFA92B498ULL},  // -0x1.8f80e06f3a04cp+4
    HardCase{0xC01DAF3AFEE51991ULL, 0x3F439C6240F78D70ULL},  // -0x1.daf3afee51991p+2
    HardCase{0xBFD07E05E6E03B83ULL, 0x3FE8BB0D03B36F1CULL},  // -0x1.07e05e6e03b83p-2
    HardCase{0xBFBDAF693D64FADAULL, 0x3FEC7F14AF0A08EBULL},  // -0x1.daf693d64fadap-4
    HardCase{0xBFBA8F783D749A8FULL, 0x3FECD8ABD4DE5C33ULL},  // -0x1.a8f783d749a8fp-4
    HardCase{0xBF940A990279C2C0ULL, 0x3FEF613A44B93CDCULL},  // -0x1.40a990279c2cp-6
    HardCase{0xBF2A2FEFEFD580DFULL, 0x3FEFFE5D0BB7EABFULL},  // -0x1.a2fefefd580dfp-13
    HardCase{0xBF1664716B68A409ULL, 0x3FEFFF4CDE6A0BFBULL},  // -0x1.664716b68a409p-14
    HardCase{0xBE30401AE48409B5ULL, 0x3FEFFFFFFDF7FCA3ULL},  // -0x1.0401ae48409b5p-28
    HardCase{0xBE1FF171507F8BA5ULL, 0x3FEFFFFFFF007475ULL},  // -0x1.ff171507f8ba5p-30
    HardCase{0xBE185E60704A3A9CULL, 0x3FEFFFFFFF3D0CFDULL},  // -0x1.85e60704a3a9cp-30
    HardCase{0xBE04BD46601AE1EFULL, 0x3FEFFFFFFFAD0AE7ULL},  // -0x1.4bd46601ae1efp-31
    HardCase{0x3CA0000000000000ULL, 0x3FF0000000000001ULL},  // 0x1p-53
    HardCase{0x3DBFFFF7FFFE0001ULL, 0x3FF000000001FFFFULL},  // 0x1.ffff7fffe0001p-36
    HardCase{0x3DD8CD99FFFB319CULL, 0x3FF0000000063367ULL},  // 0x1.8cd99fffb319cp-34
    HardCase{0x3E0413F33FE6CE07ULL, 0x3FF00000002827E7ULL},  // 0x1.413f33fe6ce07p-31
    HardCase{0x3E1F8E165F8388F7ULL, 0x3FF00000007E3859ULL},  // 0x1.f8e165f8388f7p-30
    HardCase{0x3EB282AAE3169EE7ULL, 0x3FF00001282AB8E7ULL},  // 0x1.282aae3169ee7p-20
    HardCase{0x3EBE5BA92AA9B1EFULL, 0x3FF00001E5BAAF77ULL},  // 0x1.e5ba92aa9b1efp-20
    HardCase{0x3F76A4D1AF9CC989ULL, 0x3FF016B4DF3299D7ULL},  // 0x1.6a4d1af9cc989p-8
    HardCase{0x3FFACA7AE8DA5A7BULL, 0x401557D4ACD7E557ULL},  // 0x1.aca7ae8da5a7bp+0
    // The hardest input of each range of the coverage table.
    HardCase{0xC0412D31A20FB38BULL, 0x3CD5B0BF3244820AULL},  // -0x1.12d31a20fb38bp+5, the list
    HardCase{0x3E09E9CBBFD6080BULL, 0x3FF000000033D398ULL},  // 0x1.9e9cbbfd6080bp-31, the own check
    HardCase{0x3C9FFFFFFFFFFFFFULL, 0x3FF0000000000000ULL},  // 0x1.fffffffffffffp-54, not on the list
    HardCase{0xBC90000000000000ULL, 0x3FF0000000000000ULL},  // -0x1p-54
    HardCase{0xBC90000000000001ULL, 0x3FEFFFFFFFFFFFFFULL},  // -0x1.0000000000001p-54, not on the list
    HardCase{0xBF3848D160BE3A2FULL, 0x3FEFFCF70AAE7D6DULL},  // -0x1.848d160be3a2fp-12, -m 44
    HardCase{0xBF313F927BC8A2BEULL, 0x3FEFFDD820482FE4ULL},  // -0x1.13f927bc8a2bep-12, -m 35
    HardCase{0xC086D9F7DD15F748ULL, 0x0000000000083244ULL},  // -0x1.6d9f7dd15f748p+9, subnormal
    HardCase{0xC0871EA3AA0D89AEULL, 0x0000000000000064ULL},  // -0x1.71ea3aa0d89aep+9, subnormal
    HardCase{0xC08629D4A7C6AA33ULL, 0x0006F5BEFA7682F0ULL},  // -0x1.629d4a7c6aa33p+9, subnormal
    HardCase{0xC08622BE838B427BULL, 0x0010E0A58BE7B97EULL},  // -0x1.622be838b427bp+9, near 2^-1022
    HardCase{0xC08620906089E7EFULL, 0x00162A37DA3426BEULL},  // -0x1.620906089e7efp+9, near 2^-1022
    HardCase{0xBF78E4AAAD97ED04ULL, 0x3FEFCE5D515ACB06ULL},  // -0x1.8e4aaad97ed04p-8
    HardCase{0x3F1BA07D73250DE7ULL, 0x3FF0006E83736F8DULL},  // 0x1.ba07d73250de7p-14
    // The edges of the paths.
    HardCase{0x40862E42FEFA39EFULL, 0x7FEFFFFFFFFFFF2AULL},  // the largest input with a finite result
    HardCase{0x40862E42FEFA39F0ULL, 0x7FF0000000000000ULL},  // +inf
    HardCase{0xC0874910D52D3052ULL, 0x0000000000000000ULL},  // the largest input with the result +0
    HardCase{0xC0874910D52D3051ULL, 0x0000000000000001ULL},  // the smallest subnormal result
    HardCase{0xC086232BDD7ABCD2ULL, 0x001000000000007CULL},  // the smallest input with a normal result
    HardCase{0xC086232BDD7ABCD3ULL, 0x000FFFFFFFFFFE7CULL},  // the largest subnormal result
    HardCase{0x407FFFFFFFFFFFFFULL, 0x6E19476504BA839AULL},  // below 512
    HardCase{0x4080000000000000ULL, 0x6E19476504BA852EULL},  // 512
    HardCase{0xC07FFFFFFFFFFFFFULL, 0x11C44109EDB20A75ULL},  // above -512
    HardCase{0xC080000000000000ULL, 0x11C44109EDB20931ULL},  // -512
    HardCase{0x3C8FFFFFFFFFFFFFULL, 0x3FF0000000000000ULL},  // below 2^-54
    HardCase{0x3C90000000000000ULL, 0x3FF0000000000000ULL},  // 2^-54
    HardCase{0xBC8FFFFFFFFFFFFFULL, 0x3FF0000000000000ULL},  // above -2^-54
    HardCase{0x3FF0000000000000ULL, 0x4005BF0A8B145769ULL},  // 1
    HardCase{0xBFF0000000000000ULL, 0x3FD78B56362CEF38ULL},  // -1
    HardCase{0xC087400000000000ULL, 0x0000000000000002ULL},  // -744
};

}  // namespace test_fp_exp
