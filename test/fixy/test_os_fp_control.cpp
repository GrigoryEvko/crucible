// The FP control door of foundation/os/FpControl.h: the encoding of each
// mode, the scope and its epochs, and one kernel that runs with flush to
// zero and without it on the same thread and gives the bits of each mode.
//
// The kernels are calls that the optimizer cannot read, so no product folds
// at compile time and no product moves across a write of the register.

#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

#include <cstdint>

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The bits of 1e-30f * 1e-10f with round to nearest and no flush: the
// subnormal 71362 * 2^-149, far from the bounds of the subnormal range, so
// the two points where the targets detect tininess give one answer.
constexpr std::uint32_t kSubnormalProductBits = 0x000116C2u;

[[gnu::noipa, nodiscard]] float multiply(float value, float factor) noexcept { return value * factor; }

[[gnu::noipa, nodiscard]] float add(float value, float addend) noexcept { return value + addend; }

[[nodiscard]] std::uint32_t bits_of(float value) noexcept { return ::fixy::bit_cast<std::uint32_t>(value); }

[[nodiscard]] float float_of(std::uint32_t bits) noexcept { return ::fixy::bit_cast<float>(bits); }

// The kernel: one product whose exact value is subnormal.
[[nodiscard]] std::uint32_t run_subnormal_kernel() noexcept { return bits_of(multiply(1e-30F, 1e-10F)); }

// The mode of index 0 to 15: two bits of rounding, one of FTZ, one of DAZ.
[[nodiscard]] core::FpMode mode_of_index(unsigned index) noexcept {
    return core::FpMode{
        .rounding = ::fixy::enum_from<core::RoundingMode>(static_cast<std::uint8_t>(index & 3u))
                        .expect("two bits name each of the four directions"),
        .ftz = (index & 4u) != 0 ? core::FpFtz::FlushToZero : core::FpFtz::PreserveSubnormals,
        .denormal_input =
            (index & 8u) != 0 ? core::FpDenormalInput::DenormalsAreZero : core::FpDenormalInput::HonorDenormals,
    };
}

// Each mode that this target encodes goes into a word and comes back out.
void test_each_mode_comes_back() {
    os::FpControlWord const word = os::FpControlWord::read();
    int encoded = 0;
    for (unsigned index = 0; index < 16; ++index) {
        core::FpMode const mode = mode_of_index(index);
        if (!os::is_encodable_fp_mode(mode)) continue;
        CRUCIBLE_FATAL_INVARIANT(word.with_mode(mode).mode() == mode);
        ++encoded;
    }
#if defined(__x86_64__)
    CRUCIBLE_FATAL_INVARIANT(encoded == 16);
#else
    CRUCIBLE_FATAL_INVARIANT(encoded == 8);
#endif
    CRUCIBLE_FATAL_INVARIANT(os::is_encodable_fp_mode(core::kFpModeIeee));
    CRUCIBLE_FATAL_INVARIANT(os::is_encodable_fp_mode(core::kFpModeFlushToZero));
    // The process starts in the mode of IEEE 754.
    CRUCIBLE_FATAL_INVARIANT(word.mode() == core::kFpModeIeee);
}

// The kernel runs one time with flush to zero and one time without it, on
// the same thread.  Each run gives the bits of its mode, and the end of a
// scope gives the register back.
void test_the_kernel_gives_the_bits_of_each_mode() {
    CRUCIBLE_FATAL_INVARIANT(run_subnormal_kernel() == kSubnormalProductBits);
    {
        os::FpScope<core::kFpModeFlushToZero> const scope = os::mint_fp_scope<core::kFpModeFlushToZero>();
        CRUCIBLE_FATAL_INVARIANT(os::FpControlWord::read().mode() == core::kFpModeFlushToZero);
        CRUCIBLE_FATAL_INVARIANT(run_subnormal_kernel() == 0u);
        {
            os::FpScope<core::kFpModeIeee> const inner = os::mint_fp_scope<core::kFpModeIeee>();
            CRUCIBLE_FATAL_INVARIANT(run_subnormal_kernel() == kSubnormalProductBits);
        }
        CRUCIBLE_FATAL_INVARIANT(run_subnormal_kernel() == 0u);
    }
    CRUCIBLE_FATAL_INVARIANT(os::FpControlWord::read().mode() == core::kFpModeIeee);
    CRUCIBLE_FATAL_INVARIANT(run_subnormal_kernel() == kSubnormalProductBits);
}

// DAZ reads a subnormal input as zero.  x86_64 has a bit of its own for it,
// and aarch64 without FEAT_AFP flushes the inputs with the results.
void test_a_subnormal_input_reads_as_zero() {
    float const subnormal = float_of(kSubnormalProductBits);
    CRUCIBLE_FATAL_INVARIANT(bits_of(multiply(subnormal, 1e20F)) != 0u);
    {
        os::FpScope<core::kFpModeFlushToZero> const scope = os::mint_fp_scope<core::kFpModeFlushToZero>();
        CRUCIBLE_FATAL_INVARIANT(bits_of(multiply(subnormal, 1e20F)) == 0u);
    }
#if defined(__x86_64__)
    constexpr core::FpMode kInputsOnly{.denormal_input = core::FpDenormalInput::DenormalsAreZero};
    {
        os::FpScope<kInputsOnly> const scope = os::mint_fp_scope<kInputsOnly>();
        CRUCIBLE_FATAL_INVARIANT(bits_of(multiply(subnormal, 1e20F)) == 0u);
        CRUCIBLE_FATAL_INVARIANT(run_subnormal_kernel() == kSubnormalProductBits);
    }
#endif
}

// The rounding field: 1 + 2^-24 is a tie, which the nearest mode rounds to
// even and the upward mode rounds up.
void test_the_rounding_of_each_direction() {
    float const half_ulp = float_of(0x33800000u);
    CRUCIBLE_FATAL_INVARIANT(bits_of(add(1.0F, half_ulp)) == 0x3F800000u);
    constexpr core::FpMode kUpward{.rounding = core::RoundingMode::RP};
    constexpr core::FpMode kDownward{.rounding = core::RoundingMode::RM};
    {
        os::FpScope<kUpward> const scope = os::mint_fp_scope<kUpward>();
        CRUCIBLE_FATAL_INVARIANT(bits_of(add(1.0F, half_ulp)) == 0x3F800001u);
    }
    {
        os::FpScope<kDownward> const scope = os::mint_fp_scope<kDownward>();
        CRUCIBLE_FATAL_INVARIANT(bits_of(add(-1.0F, -half_ulp)) == 0xBF800001u);
        CRUCIBLE_FATAL_INVARIANT(bits_of(add(1.0F, half_ulp)) == 0x3F800000u);
    }
    CRUCIBLE_FATAL_INVARIANT(bits_of(add(1.0F, half_ulp)) == 0x3F800000u);
}

// The epoch of a scope is the innermost epoch of its thread while the scope
// lives and no inner scope is open.
void test_the_epoch_of_the_innermost_scope() {
    std::uint64_t outer_epoch = 0;
    {
        os::FpScope<core::kFpModeIeee> const outer = os::mint_fp_scope<core::kFpModeIeee>();
        outer_epoch = outer.epoch();
        CRUCIBLE_FATAL_INVARIANT(outer_epoch != 0 && os::is_innermost_fp_scope(outer_epoch));
        {
            os::FpScope<core::kFpModeFlushToZero> const inner = os::mint_fp_scope<core::kFpModeFlushToZero>();
            CRUCIBLE_FATAL_INVARIANT(inner.epoch() != outer_epoch);
            CRUCIBLE_FATAL_INVARIANT(os::is_innermost_fp_scope(inner.epoch()));
            CRUCIBLE_FATAL_INVARIANT(!os::is_innermost_fp_scope(outer_epoch));
        }
        CRUCIBLE_FATAL_INVARIANT(os::is_innermost_fp_scope(outer_epoch));
    }
    CRUCIBLE_FATAL_INVARIANT(!os::is_innermost_fp_scope(outer_epoch));
    CRUCIBLE_FATAL_INVARIANT(!os::is_innermost_fp_scope(0));
}

}  // namespace

int main() {
    test_each_mode_comes_back();
    test_the_kernel_gives_the_bits_of_each_mode();
    test_a_subnormal_input_reads_as_zero();
    test_the_rounding_of_each_direction();
    test_the_epoch_of_the_innermost_scope();
    return 0;
}
