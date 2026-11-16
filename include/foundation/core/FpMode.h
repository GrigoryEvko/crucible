#pragma once

// The floating-point mode of a thread: the direction of rounding, the flush
// of a subnormal result to zero, and the read of a subnormal input as zero.
//
// The FP control register of each thread holds these three settings, and
// foundation/os/FpControl.h is the door that reads and writes it.  The
// result of an operation on floats is a function of its inputs and of this
// mode.  Thus a kernel context states the mode in its type
// (foundation/effects/Ctx.h), and the atom fixy::atom::fp::mode names the
// two flush settings of a binding.  This header is the one vocabulary of the
// three settings for the base.
//
// The first enumerator of each enum is the mode of a process at start:
// round to nearest with ties to even, subnormal results kept and subnormal
// inputs read.

#include <cstdint>

namespace foundation::core {

enum class RoundingMode : std::uint8_t {
    RN = 0,  // to nearest, ties to even
    RZ = 1,  // toward zero
    RM = 2,  // toward minus infinity
    RP = 3,  // toward plus infinity
};

enum class FpFtz : std::uint8_t {
    PreserveSubnormals = 0,
    FlushToZero = 1,
};

enum class FpDenormalInput : std::uint8_t {
    HonorDenormals = 0,
    DenormalsAreZero = 1,
};

// The three settings as one value.  The type is structural, so a mode is a
// template argument of a kernel context.  The three fields fill three
// bytes with no padding.
struct FpMode {
    RoundingMode rounding = RoundingMode::RN;
    FpFtz ftz = FpFtz::PreserveSubnormals;
    FpDenormalInput denormal_input = FpDenormalInput::HonorDenormals;

    [[nodiscard]] friend constexpr bool operator==(FpMode, FpMode) noexcept = default;
};

// The mode of a process at start, which IEEE 754 names as its default.
inline constexpr FpMode kFpModeIeee{};

// The flush mode: round to nearest, a subnormal result is zero, and a
// subnormal input reads as zero.  Each target of the tree has a register
// word for it.
inline constexpr FpMode kFpModeFlushToZero{
    .rounding = RoundingMode::RN,
    .ftz = FpFtz::FlushToZero,
    .denormal_input = FpDenormalInput::DenormalsAreZero,
};

}  // namespace foundation::core
