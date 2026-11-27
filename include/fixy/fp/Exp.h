#pragma once

// exp of a double and of a float, with the same bits on each target and each
// level of the instruction set.
//
//   fixy::fp::exp(double)   error of at most 0.52 units in the last place
//   fixy::fp::exp(float)    the correctly rounded result for each input
//
// Each function is total.  A NaN gives the canonical quiet NaN quiet_nan_of<T>,
// so the bits do not depend on the payload rules of the host.  +inf gives
// +inf, -inf gives +0, and a result past the largest finite value gives +inf.
// A result below the smallest subnormal value gives +0.  No function reads or
// writes errno or a flag of the floating-point unit on purpose, and no caller
// can read a flag of them.
//
// Mode of the register
//   Each function assumes the default mode of the floating-point unit: the
//   round to nearest with ties to even, no flush of a subnormal result to
//   zero, and no read of a subnormal input as zero.  Another mode changes the
//   bits.  PyTorch can set the flush of subnormal values in the vessel
//   process, and then a subnormal result changes.  A kernel scope proves the
//   mode through its FpScope token, not this header.
//
// Operations
//   Each operation is an add, a subtract or a multiply of IEEE 754, a fused
//   multiply-add of detail::fma_of_finite (fixy/fp/FmaOfFinite.h), an integer
//   operation on the bits, a read of the table, or a conversion between float
//   and double.  The order of the operations is the order of the source.  The
//   build contracts no multiply and add (-ffp-contract=off, the floor
//   crucible_fp_strict), so each fma is an explicit fma_of_finite.  It rounds
//   one time on each target: the instruction at x86-64-v3, x86-64-v4 and
//   aarch64, a form in doubles at x86-64 and x86-64-v2.  So each level gives
//   the same bits.  A NaN input returns quiet_nan_of<T> before the first fma,
//   so no fma takes a NaN.  The functions are in the
//   inline namespace CRUCIBLE_SIMD_TARGET, so a program can link the bodies of
//   two levels and compare their results.
//
// Algorithm of exp(double): the table method of Tang, as in glibc and ARM
// optimized-routines.
//   k = round(x * 128 / ln2), by the add and the subtract of 0x1.8p52.
//   r = x - k * ln2/128 in two parts, through two fma.  |r| <= ln2/256 and a
//     little more.
//   2^(k/128) = s * (1 + tail): s is the double nearest 2^(j/128) for j = k mod
//     128, with the exponent of k/128 added to its bits, and tail is the
//     relative error of s.
//   exp(x) = s + s * (tail + r + r^2 * (C2 + r C3) + r^4 * (C4 + r C5)).
//   The polynomial is the minimax polynomial of exp(r) for the relative error
//   on |r| <= ln2/256, with an error of 2^-65.4.
//   A result past the normal range scales s by 2^-1009 or 2^1022 and then
//   multiplies by an exact power of two.  A subnormal result first rounds at
//   its own precision through an add of 1 in two parts, so it rounds one time.
//   The fma sites: two in the reduction, four in the polynomial, one in the
//   last step, and three in the cold path of a large result.
//
// Algorithm of exp(float)
//   The fast path computes in double with the same reduction in one fma and
//   a polynomial of degree 3 (error 2^-41.2).  When the double result is within
//   2^13 units of its last bit of a float midpoint, the round can be wrong, and
//   the slow path takes the input.  The slow path rounds exp(double) of the
//   input, whose error is below one unit of a double.  So the round is wrong
//   only when that result is a float midpoint (fixy/fp/RoundToFloat.h), and the
//   sweep of the oracle lists each such input.  A result below 2^-126 always
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

// The minimax polynomial of exp(r) = 1 + r + r^2 * (C2 + C3 r + C4 r^2 + C5 r^3).
inline constexpr double kExpC2 = 0x1.ffffffffffdbdp-2;
inline constexpr double kExpC3 = 0x1.5555555555767p-3;
inline constexpr double kExpC4 = 0x1.55555cf144a31p-5;
inline constexpr double kExpC5 = 0x1.11110f0afd377p-7;
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
// clang-format on

// The float form: its inputs past the edges.  exp(x) rounds to +inf above
// the first edge, to +0 below the second, and is below 2^-126 below the
// third.
inline constexpr float kExpFloatOverflowAbove = 0x1.62e42ep+6f;
inline constexpr float kExpFloatZeroBelow = -0x1.9fe368p+6f;
inline constexpr float kExpFloatSubnormalBelow = -0x1.5d589ep+6f;
// The tolerance of the rounding test of the float fast path, in units of the
// last bit of the double result: the error 2^-41 of the fast path times 2^53,
// with a margin of two.
inline constexpr std::uint64_t kExpFloatTolerance = std::uint64_t{1} << 13;

// The parts of the reduction that the path of a large result reads.
struct ExpScale {
    // The bits of s_j with the exponent of k/128 added.
    std::uint64_t scale_bits = 0;
    // The bits of k + 0x1.8p52: the low 32 bits hold k.
    std::uint64_t shifted_bits = 0;
};

inline namespace CRUCIBLE_SIMD_TARGET {

// The top 12 bits of a double: the sign and the biased exponent.
[[nodiscard]] constexpr std::uint32_t exp_top12_(double value) noexcept {
    return static_cast<std::uint32_t>(__builtin_bit_cast(std::uint64_t, value) >> 52);
}

// exp of an input with 512 <= |x| < 1024: s * (1 + sum) with the exponent of
// s past the normal range.  A positive k scales s down by 2^1009 and the
// result up by 2^1009, and the last multiply overflows to +inf where the
// result does.  A negative k scales s up by 2^1022.  When the scaled result is
// below 1, the final multiply by 2^-1022 gives a subnormal value, so the add
// of 1 in two parts rounds the result at the precision of that subnormal
// value first, and the final multiply is exact.
[[gnu::cold, gnu::noinline, nodiscard]] constexpr double exp_past_range_(double sum, ExpScale reduction) noexcept {
    if ((reduction.shifted_bits & 0x80000000u) == 0) {
        double const scale = __builtin_bit_cast(double, reduction.scale_bits - (std::uint64_t{1009} << 52));
        return ::fixy::fp::detail::fma_of_finite(scale, sum, scale) * 0x1p1009;
    }
    double const scale = __builtin_bit_cast(double, reduction.scale_bits + (std::uint64_t{1022} << 52));
    double result = ::fixy::fp::detail::fma_of_finite(scale, sum, scale);
    if (result < 1.0) {
        double const low = ::fixy::fp::detail::fma_of_finite(scale, sum, scale - result);
        double const high = 1.0 + result;
        double const rest = ((1.0 - high) + result) + low;
        result = (high + rest) - 1.0;
    }
    return result * 0x1p-1022;
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
    using ::fixy::fp::detail::fma_of_finite;
    std::uint32_t top = detail::exp_top12_(value) & 0x7ffu;
    std::uint32_t constexpr tiny_top = 0x3c9u;  // the top of 2^-54
    std::uint32_t constexpr large_top = 0x408u;  // the top of 512
    std::uint32_t constexpr huge_top = 0x409u;  // the top of 1024
    if (top - tiny_top >= large_top - tiny_top) [[unlikely]] {
        if (top - tiny_top >= 0x80000000u) {
            // |x| < 2^-54: exp(x) rounds to 1, and 1 + x gives that round.
            return 1.0 + value;
        }
        if (top >= huge_top) {
            std::uint64_t const bits = __builtin_bit_cast(std::uint64_t, value);
            if (bits == 0xfff0000000000000ULL) return 0.0;
            if (top >= 0x7ffu)
                return (bits & 0x000fffffffffffffULL) != 0 ? ::foundation::core::quiet_nan_of<double> : value;
            return (bits >> 63) != 0 ? 0.0 : ::foundation::core::infinity_of<double>;
        }
        top = 0;
    }
    double const scaled = detail::kExpInvLn2N * value;
    double rounded = scaled + detail::kExpShift;
    std::uint64_t const shifted_bits = __builtin_bit_cast(std::uint64_t, rounded);
    rounded -= detail::kExpShift;
    double reduced = fma_of_finite(rounded, detail::kExpNegLn2HiN, value);
    reduced = fma_of_finite(rounded, detail::kExpNegLn2LoN, reduced);
    std::uint64_t const index = 2 * (shifted_bits & detail::kExpTableMask);
    double const tail = __builtin_bit_cast(double, detail::kExpTable[index]);
    std::uint64_t const scale_bits = detail::kExpTable[index + 1] + (shifted_bits << (52 - detail::kExpTableBits));
    double const reduced_squared = reduced * reduced;
    double const low_pair = fma_of_finite(reduced, detail::kExpC3, detail::kExpC2);
    double const high_pair = fma_of_finite(reduced, detail::kExpC5, detail::kExpC4);
    double sum = fma_of_finite(reduced_squared, low_pair, tail + reduced);
    sum = fma_of_finite(reduced_squared * reduced_squared, high_pair, sum);
    if (top == 0) [[unlikely]] {
        return detail::exp_past_range_(sum, detail::ExpScale{scale_bits, shifted_bits});
    }
    double const scale = __builtin_bit_cast(double, scale_bits);
    return fma_of_finite(scale, sum, scale);
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

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail

namespace fixy::fp {

inline namespace CRUCIBLE_SIMD_TARGET {

[[nodiscard]] constexpr float exp(float value) noexcept {
    using ::fixy::fp::detail::fma_of_finite;
    std::uint32_t const magnitude = __builtin_bit_cast(std::uint32_t, value) & 0x7fffffffu;
    if (magnitude > 0x7f800000u) [[unlikely]]
        return ::foundation::core::quiet_nan_of<float>;
    if (value > detail::kExpFloatOverflowAbove) [[unlikely]]
        return ::foundation::core::infinity_of<float>;
    if (value < detail::kExpFloatZeroBelow) [[unlikely]]
        return 0.0f;
    if (value < detail::kExpFloatSubnormalBelow) [[unlikely]]
        return detail::exp_float_slow_(value);
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
