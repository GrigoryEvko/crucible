// Tests of foundation/core/StorageFloat.h at run time, against PyTorch and
// against the conversion instructions of the host.
//
// The widen of each format holds for each 8-bit and each 16-bit pattern: it
// gives the bits of the table torch_widen.bin, which make_torch_tables.py
// wrote with Tensor.to of PyTorch.  The widen of the product arm gives the
// bits of the portable code, and the widen to double gives the value of the
// widen to float.  Each pattern goes through a widen and a round of each mode
// and comes back, apart from the NaN, which comes back quiet, and the
// infinity, which saturates.
//
// The round of each mode holds on each row of torch_round.bin: each rounding
// tie of the fp8 formats, the ties at the ends of each binade of f16 and
// bf16, the subnormal edges, the overflow edges, each NaN class and a
// fixed-seed sample.  PyTorch gives one mode for each format, and each
// difference from it has a name:
//
//   bf16 NaN         PyTorch gives 0xFFFF for each NaN.  The base keeps the
//                    sign and the top bits of the payload
//   fp8_e5m2 NaN     PyTorch gives S.11111.11 for each NaN.  The base keeps
//                    the top bit of the payload
//   no saturation    PyTorch has no saturating f16, bf16 and fp8_e5m2, so an
//                    overflow gives an infinity there
//   saturation       PyTorch has no nonsaturating fp8_e4m3, so an overflow
//                    gives 448 there and a NaN in round_nonsaturating
//
// On a host with F16C, vcvtps2ph and vcvtph2ps give the bits of the portable
// code under each MXCSR state: each rounding mode, with and without DAZ and
// FTZ.  The round runs on each row of the table and on each binary32
// subnormal, where DAZ acts.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Quarantine.h>
#include <foundation/core/FpMode.h>
#include <foundation/core/StorageFloat.h>
#include <foundation/os/FpControl.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

namespace core = ::foundation::core;
namespace detail = ::foundation::core::detail;
using detail::Saturation;

using Test = ::fixy::TestRunnerCtx;

template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

// One row of torch_round.bin: the binary32 input and the word of each format.
struct TorchRoundRow final {
    std::uint32_t input = 0;
    std::uint16_t f16 = 0;
    std::uint16_t bf16 = 0;
    std::uint8_t e4m3 = 0;
    std::uint8_t e5m2 = 0;
};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the PyTorch tables, and the F16C instructions under each MXCSR state")

constexpr unsigned char torch_widen_bytes[] = {
#embed "storage_float/torch_widen.bin"
};

constexpr unsigned char torch_round_bytes[] = {
#embed "storage_float/torch_round.bin"
};

constexpr std::size_t table_header_bytes = 128;
constexpr std::size_t round_row_bytes = 12;
constexpr std::size_t widen_words = 65536 + 65536 + 256 + 256;
constexpr std::size_t torch_round_rows = (sizeof(torch_round_bytes) - table_header_bytes) / round_row_bytes;

static_assert(sizeof(torch_widen_bytes) == table_header_bytes + 4 * widen_words);
static_assert((sizeof(torch_round_bytes) - table_header_bytes) % round_row_bytes == 0);
static_assert(torch_widen_bytes[0] == 'w' && torch_widen_bytes[6] == 'v' && torch_widen_bytes[7] == '1');
static_assert(torch_round_bytes[0] == 'r' && torch_round_bytes[6] == 'v' && torch_round_bytes[7] == '1');

[[nodiscard]] constexpr std::uint32_t little_endian_word(std::size_t at, std::size_t count, bool is_widen) noexcept {
    std::uint32_t word = 0;
    for (std::size_t index = count; index > 0; --index) {
        unsigned char const byte = is_widen ? torch_widen_bytes[at + index - 1] : torch_round_bytes[at + index - 1];
        word = (word << 8) | byte;
    }
    return word;
}

// The binary32 bits that PyTorch gives for word index of the widen table.
[[nodiscard]] std::uint32_t torch_widen_word(std::size_t index) noexcept {
    CRUCIBLE_FATAL_INVARIANT(index < widen_words);
    return little_endian_word(table_header_bytes + 4 * index, 4, true);
}

[[nodiscard]] TorchRoundRow torch_round_row(std::size_t index) noexcept {
    CRUCIBLE_FATAL_INVARIANT(index < torch_round_rows);
    std::size_t const at = table_header_bytes + round_row_bytes * index;
    return TorchRoundRow{
        .input = little_endian_word(at, 4, false),
        .f16 = static_cast<std::uint16_t>(little_endian_word(at + 4, 2, false)),
        .bf16 = static_cast<std::uint16_t>(little_endian_word(at + 6, 2, false)),
        .e4m3 = static_cast<std::uint8_t>(little_endian_word(at + 8, 1, false)),
        .e5m2 = static_cast<std::uint8_t>(little_endian_word(at + 9, 1, false)),
    };
}

#if defined(__x86_64__)

[[nodiscard]] bool host_has_f16c() noexcept { return __builtin_cpu_supports("f16c") != 0; }

// The MXCSR states: each rounding mode, with and without DAZ and FTZ.  The
// door of foundation/os/FpControl.h writes each state, and it keeps each
// exception masked as the process started.
[[nodiscard]] ::foundation::core::FpMode mxcsr_state(int index) noexcept {
    using ::foundation::core::FpDenormalInput;
    using ::foundation::core::FpFtz;
    using ::foundation::core::RoundingMode;
    auto const step = static_cast<unsigned>(index);
    // The order of the two bits of MXCSR.RC: nearest, down, up, toward zero.
    RoundingMode rounding = RoundingMode::RZ;
    switch (step & 3u) {
        case 0u:
            rounding = RoundingMode::RN;
            break;
        case 1u:
            rounding = RoundingMode::RM;
            break;
        case 2u:
            rounding = RoundingMode::RP;
            break;
        default:
            break;
    }
    return ::foundation::core::FpMode{
        .rounding = rounding,
        .ftz = (step & 8u) != 0 ? FpFtz::FlushToZero : FpFtz::PreserveSubnormals,
        .denormal_input = (step & 4u) != 0 ? FpDenormalInput::DenormalsAreZero : FpDenormalInput::HonorDenormals,
    };
}

// The instructions through calls that the optimizer cannot read, so each
// call stays between the two writes of MXCSR.
[[gnu::noipa, gnu::target("f16c"), nodiscard]] std::uint32_t widen_by_f16c(std::uint16_t word) noexcept {
    return detail::widen_f16_by_f16c(word);
}

[[gnu::noipa, gnu::target("f16c"), nodiscard]] std::uint16_t round_by_f16c(std::uint32_t bits) noexcept {
    return detail::round_f16_by_f16c(bits);
}

[[gnu::noipa, gnu::target("f16c"), nodiscard]] std::uint16_t round_saturating_by_f16c(std::uint32_t bits) noexcept {
    return detail::round_f16_saturating_by_f16c(bits);
}

// The positive control: vcvtps2ph with the immediate 4 reads MXCSR.RC, as
// the cast (_Float16)x of GCC does.  It differs from the portable code under
// each rounding mode other than the nearest, which shows that the state is
// in effect and that the instruction runs.
[[gnu::noipa, gnu::target("f16c"), nodiscard]] std::uint16_t round_by_f16c_with_mxcsr(std::uint32_t bits) noexcept {
    detail::HardwareFloatLanes const lanes{::fixy::bit_cast<float>(bits), 0.0F, 0.0F, 0.0F};
    detail::HardwareHalfLanes const halves = __builtin_ia32_vcvtps2ph(lanes, 4);
    return static_cast<std::uint16_t>(halves[0]);
}

// The counts of one MXCSR state: the inputs whose F16C bits differ from the
// portable bits, and the rows where the control differs.
struct MxcsrCounts final {
    std::uint64_t differences = 0;
    std::uint64_t control_differences = 0;
};

// The counts of one MXCSR state: each f16 word for the widen, and each row
// of the round table and each binary32 subnormal for the two rounds.  MXCSR
// goes back to its state before the return.
[[gnu::noipa, nodiscard]] MxcsrCounts count_f16c_differences_under(::foundation::core::FpMode mode) noexcept {
    std::uint64_t differences = 0;
    std::uint64_t control_differences = 0;
    ::foundation::os::FpControlWord const saved = ::foundation::os::FpControlWord::read();
    ::foundation::os::write_fp_control(saved.with_mode(mode));
    for (std::uint32_t word = 0; word < 65536; ++word) {
        auto const half = static_cast<std::uint16_t>(word);
        bool const differs = widen_by_f16c(half) != detail::widen_portable<detail::Binary16Format>(half);
        differences += differs ? std::uint64_t{1} : std::uint64_t{0};
    }
    auto const count_round = [&differences](std::uint32_t bits) noexcept {
        using detail::Binary16Format;
        bool const differs =
            round_by_f16c(bits) != detail::round_portable<Binary16Format, Saturation::nonsaturating>(bits);
        bool const saturating_differs =
            round_saturating_by_f16c(bits) != detail::round_portable<Binary16Format, Saturation::saturating>(bits);
        differences += (differs ? std::uint64_t{1} : std::uint64_t{0})
                     + (saturating_differs ? std::uint64_t{1} : std::uint64_t{0});
    };
    for (std::size_t row = 0; row < torch_round_rows; ++row) {
        std::uint32_t const input = torch_round_row(row).input;
        count_round(input);
        bool const control_differs = round_by_f16c_with_mxcsr(input)
                                  != detail::round_portable<detail::Binary16Format, Saturation::nonsaturating>(input);
        control_differences += control_differs ? std::uint64_t{1} : std::uint64_t{0};
    }
    for (std::uint32_t magnitude = 1; magnitude < 0x00800000u; ++magnitude) {
        count_round(magnitude);
        count_round(magnitude | 0x80000000u);
    }
    ::foundation::os::write_fp_control(saved);
    return MxcsrCounts{.differences = differences, .control_differences = control_differences};
}

#endif

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The position of the first word of each format in the widen table.
template <class T>
inline constexpr std::size_t widen_table_offset = 0;
template <>
inline constexpr std::size_t widen_table_offset<core::bf16> = 65536;
template <>
inline constexpr std::size_t widen_table_offset<core::fp8_e4m3> = 131072;
template <>
inline constexpr std::size_t widen_table_offset<core::fp8_e5m2> = 131328;

template <class T>
inline constexpr std::uint32_t pattern_count = std::uint32_t{1} << (8 * sizeof(T));

template <class T>
using FactsOf = detail::StorageFacts<typename T::FormatType>;

// The widen of each pattern gives the bits of PyTorch, and the product arm
// gives the bits of the portable code.  The widen to double gives the value
// of the widen to float, and a signaling NaN of bf16 keeps its bits.
template <class T>
void check_widen(Test const& test) {
    using Word = typename T::Word;
    std::uint32_t torch_differences = 0;
    for (std::uint32_t word = 0; word < pattern_count<T>; ++word) {
        T const value = T::from_bits(opaque(static_cast<Word>(word)));
        auto const wide = ::fixy::bit_cast<std::uint32_t>(core::widen<float>(value));
        CRUCIBLE_TEST_ASSERT(test, wide == detail::widen_portable<typename T::FormatType>(static_cast<Word>(word)));
        torch_differences += wide != torch_widen_word(widen_table_offset<T> + word) ? 1u : 0u;
        auto const wider = ::fixy::bit_cast<std::uint64_t>(core::widen<double>(value));
        bool const is_signaling = (wide & 0x7FC00000u) == 0x7F800000u && (wide & 0x003FFFFFu) != 0;
        if (is_signaling) {
            CRUCIBLE_TEST_ASSERT(test, wider
                                           == ((std::uint64_t{wide >> 31} << 63) | 0x7FF0000000000000u
                                               | (std::uint64_t{wide & 0x007FFFFFu} << 29)));
        } else {
            double const reference = static_cast<double>(::fixy::bit_cast<float>(wide));
            CRUCIBLE_TEST_ASSERT(test, wider == ::fixy::bit_cast<std::uint64_t>(reference));
        }
    }
    CRUCIBLE_TEST_ASSERT(test, torch_differences == 0);
}

// A widen and a round of each mode give the pattern back.  A NaN comes back
// quiet, and an infinity comes back as the highest value from the
// saturating round.
template <class T>
void check_round_trip(Test const& test) {
    using Word = typename T::Word;
    using Facts = FactsOf<T>;
    for (std::uint32_t word = 0; word < pattern_count<T>; ++word) {
        float const wide = core::widen<float>(T::from_bits(static_cast<Word>(word)));
        std::uint32_t const magnitude = word & Facts::magnitude_mask;
        std::uint32_t const sign = word & ~Facts::magnitude_mask;
        std::uint32_t nonsaturating = word;
        std::uint32_t saturating = word;
        if (detail::is_nan_code<typename T::FormatType>(magnitude)) {
            nonsaturating = T::FormatType::has_infinity ? word | Facts::quiet_bit : word;
            saturating = nonsaturating;
        } else if (T::FormatType::has_infinity && magnitude == Facts::overflow) {
            saturating = sign | Facts::highest;
        }
        CRUCIBLE_TEST_ASSERT(test, core::round_nonsaturating<T>(opaque(wide)).raw() == nonsaturating);
        CRUCIBLE_TEST_ASSERT(test, core::round_saturating<T>(opaque(wide)).raw() == saturating);
    }
}

// The counts of the named differences from PyTorch on the round table.
struct TorchDifferences final {
    std::uint32_t bf16_nan = 0;
    std::uint32_t e5m2_nan = 0;
    std::uint32_t no_saturation = 0;
    std::uint32_t saturation = 0;
};

[[nodiscard]] bool is_nan_input(std::uint32_t bits) noexcept { return (bits & 0x7FFFFFFFu) > 0x7F800000u; }

// Each round of the product arm against the PyTorch word of the row.  A
// difference outside the named classes ends the test.
template <class T, Saturation Mode>
void compare_with_torch(Test const& test, std::uint32_t input, std::uint32_t torch_word,
                        TorchDifferences& differences) {
    using Facts = FactsOf<T>;
    float const value = ::fixy::bit_cast<float>(input);
    std::uint32_t const ours = Mode == Saturation::saturating ? core::round_saturating<T>(opaque(value)).raw()
                                                              : core::round_nonsaturating<T>(opaque(value)).raw();
    CRUCIBLE_TEST_ASSERT(test, (ours == detail::round_portable<typename T::FormatType, Mode>(input)));
    if (ours == torch_word) return;
    std::uint32_t const sign = torch_word & ~Facts::magnitude_mask;
    std::uint32_t const torch_magnitude = torch_word & Facts::magnitude_mask;
    if (std::is_same_v<T, core::bf16> && is_nan_input(input) && torch_word == 0xFFFFu) {
        ++differences.bf16_nan;
        return;
    }
    if (std::is_same_v<T, core::fp8_e5m2> && is_nan_input(input) && torch_word == (sign | Facts::magnitude_mask)) {
        ++differences.e5m2_nan;
        return;
    }
    if (Mode == Saturation::saturating && T::FormatType::has_infinity && torch_magnitude == Facts::overflow
        && ours == (sign | Facts::highest)) {
        ++differences.no_saturation;
        return;
    }
    if (Mode == Saturation::nonsaturating && !T::FormatType::has_infinity && torch_magnitude == Facts::highest
        && ours == (sign | Facts::overflow)) {
        ++differences.saturation;
        return;
    }
    CRUCIBLE_TEST_ASSERT(test, ours == torch_word);
}

void test_round_against_torch(Test const& test) {
    TorchDifferences differences{};
    for (std::size_t index = 0; index < torch_round_rows; ++index) {
        TorchRoundRow const row = torch_round_row(index);
        compare_with_torch<core::f16, Saturation::nonsaturating>(test, row.input, row.f16, differences);
        compare_with_torch<core::f16, Saturation::saturating>(test, row.input, row.f16, differences);
        compare_with_torch<core::bf16, Saturation::nonsaturating>(test, row.input, row.bf16, differences);
        compare_with_torch<core::bf16, Saturation::saturating>(test, row.input, row.bf16, differences);
        compare_with_torch<core::fp8_e4m3, Saturation::nonsaturating>(test, row.input, row.e4m3, differences);
        compare_with_torch<core::fp8_e4m3, Saturation::saturating>(test, row.input, row.e4m3, differences);
        compare_with_torch<core::fp8_e5m2, Saturation::nonsaturating>(test, row.input, row.e5m2, differences);
        compare_with_torch<core::fp8_e5m2, Saturation::saturating>(test, row.input, row.e5m2, differences);
    }
    // Each named class occurs in the table, so a change of PyTorch or of the
    // table that removes one shows here.
    CRUCIBLE_TEST_ASSERT(test, differences.bf16_nan > 0 && differences.e5m2_nan > 0);
    CRUCIBLE_TEST_ASSERT(test, differences.no_saturation > 0 && differences.saturation > 0);
    ::fixy::check::pass(test,
                        "the round table has {} rows.  The named differences from PyTorch: {} of the bf16 NaN, {} of "
                        "the fp8_e5m2 NaN, {} with no saturation in PyTorch, {} with saturation in PyTorch\n",
                        static_cast<std::uint64_t>(torch_round_rows), std::uint64_t{differences.bf16_nan},
                        std::uint64_t{differences.e5m2_nan}, std::uint64_t{differences.no_saturation},
                        std::uint64_t{differences.saturation});
}

void test_f16c_under_each_mxcsr_state(Test const& test) {
#if defined(__x86_64__)
    if (!host_has_f16c()) {
        ::fixy::check::pass(test, "the host has no F16C, so no instruction is compared\n");
        return;
    }
    std::uint64_t control_differences = 0;
    for (int state = 0; state < 16; ++state) {
        MxcsrCounts const counts = count_f16c_differences_under(mxcsr_state(state));
        bool const is_nearest = (state & 3) == 0;
        CRUCIBLE_TEST_ASSERT(test, counts.differences == 0);
        CRUCIBLE_TEST_ASSERT(test, is_nearest ? counts.control_differences == 0 : counts.control_differences > 0);
        control_differences += counts.control_differences;
    }
    ::fixy::check::pass(test,
                        "F16C gives the bits of the portable code under each of the 16 MXCSR states.  The control, "
                        "which reads MXCSR.RC, differs on {} rows\n",
                        control_differences);
#else
    ::fixy::check::pass(test, "the target is not x86_64, so no instruction is compared\n");
#endif
}

}  // namespace

int main() {
    Test const test{::foundation::effects::testing::test()};
    check_widen<core::f16>(test);
    check_widen<core::bf16>(test);
    check_widen<core::fp8_e4m3>(test);
    check_widen<core::fp8_e5m2>(test);
    check_round_trip<core::f16>(test);
    check_round_trip<core::bf16>(test);
    check_round_trip<core::fp8_e4m3>(test);
    check_round_trip<core::fp8_e5m2>(test);
    ::fixy::check::pass(test, "each widen gives the bits of PyTorch, and each pattern comes back from a round\n");
    test_round_against_torch(test);
    test_f16c_under_each_mxcsr_state(test);
    return 0;
}
