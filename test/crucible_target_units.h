#pragma once

// The probes of the two units of test_crucible_target_units.  The build
// compiles one unit for the baseline level x86-64-v3 and one unit for the
// level x86-64-v4, and links the two into one program.  Each unit gives
// a constant record of what its own compile sees.  The record is constant
// data, so a read of it runs no instruction of the unit.

#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>

namespace crucible::test {

// The bits of the homes of a record.  A set bit tells that the probed names
// of that header are in the target namespace of the unit (the target
// namespace of foundation/Platform.h).
inline constexpr std::uint64_t swiss_home_bit = std::uint64_t{1} << 0;
inline constexpr std::uint64_t philox_home_bit = std::uint64_t{1} << 1;
inline constexpr std::uint64_t fec_home_bit = std::uint64_t{1} << 2;
inline constexpr std::uint64_t fountain_home_bit = std::uint64_t{1} << 3;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body tells which unit of the link holds it")

// The record of one unit:
//   - homes: the home bits of the headers whose probed names are in the
//     target namespace of the unit
//   - mul_xor_body: the body of crucible::cntp::detail::mul_xor, whose type
//     reads no target
//   - kernel_bits_body: the bits of the kernels of the three headers on fixed
//     inputs, by the body of the unit.  The unit of x86-64-v4 runs only on
//     such a processor.
struct CrucibleTargetUnit {
    std::uint64_t homes = 0;
    void (*mul_xor_body)(::foundation::core::View<std::byte>, ::foundation::core::View<std::byte const>,
                         std::uint8_t) noexcept = nullptr;
    std::uint64_t (*kernel_bits_body)() noexcept = nullptr;
};

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The record of the unit of x86-64-v3, and of the unit of x86-64-v4.
extern CrucibleTargetUnit const baseline_crucible_unit;
extern CrucibleTargetUnit const wide_crucible_unit;

}  // namespace crucible::test
