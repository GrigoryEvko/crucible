#pragma once

// The probes of the two units of test_target_units.  The build compiles one
// unit for the baseline target of x86-64 and one unit for the target of
// x86-64-v4, and links the two into one program.  Each unit gives a constant
// record of the addresses of the probed bodies and objects of the base, as
// its own compile sees them.  The record is constant data, so a read of it
// runs no instruction of the unit.

#include <foundation/Quarantine.h>
#include <foundation/core/Choice.h>
#include <foundation/core/KeyedHash.h>
#include <foundation/core/Region.h>
#include <foundation/core/StorageFloat.h>

#include <cstddef>
#include <cstdint>

namespace crucible::test {

// The brand of the output of the probed scan.  The probe takes an address and
// makes no frame, so a plain brand names the body.
struct TargetProbeBrand final {};

// The key of the probed keyed hash: the bytes 0 to 31.
[[nodiscard]] constexpr ::foundation::core::KeyedHashKeyBytes target_probe_key() noexcept {
    ::foundation::core::FixedArray<std::byte, 32> const bytes =
        ::foundation::core::build_fixed_array<std::byte, 32>([](auto run) noexcept {
            std::uint8_t next = 0;
            for (std::byte& byte : run) {
                byte = static_cast<std::byte>(next);
                ++next;
            }
        });
    return ::foundation::core::KeyedHashKeyBytes{bytes.view()};
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")

// The addresses that one unit gives:
//   - sum_by_tree_body, sum_scan_body, fixed_copy_body: three bodies whose source
//     reads the vector width of the target
//   - fma_exact_body: the exact form of fp::fma for a double, an out-of-line
//     body whose instructions change with the level of the target
//   - vector_bytes: the object of the vector width that the target sets
//   - sum_bits_body: the bits of the tree sum of the run of the test, by the
//     body of the unit.  The unit of x86-64-v4 runs only on such a processor.
//   - keyed_hash_body: the keyed hash of a message under target_probe_key(),
//     by a call that the unit compiles.  The call goes to the run-time arm of
//     the foundation library, which the build compiles for its own level
//   - widen_f16_body, round_f16_body, round_f16_saturating_body: the three
//     conversions of f16, whose source reads F16C of the target
struct TargetUnitBodies {
    float (*sum_by_tree_body)(::foundation::core::View<float const>) noexcept = nullptr;
    ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::LengthMismatch> (*sum_scan_body)(
        ::foundation::core::View<float const>, ::foundation::core::ViewMut<float, TargetProbeBrand>) noexcept = nullptr;
    void (*fixed_copy_body)(::foundation::core::View<std::uint8_t, 64>,
                            ::foundation::core::View<std::uint8_t const, 64>) noexcept = nullptr;
    double (*fma_exact_body)(double, double, double) noexcept = nullptr;
    std::size_t const* vector_bytes = nullptr;
    std::uint32_t (*sum_bits_body)() noexcept = nullptr;
    ::foundation::core::Hash256 (*keyed_hash_body)(::foundation::core::View<std::byte const>) noexcept = nullptr;
    float (*widen_f16_body)(::foundation::core::f16) noexcept = nullptr;
    ::foundation::core::f16 (*round_f16_body)(float) noexcept = nullptr;
    ::foundation::core::f16 (*round_f16_saturating_body)(float) noexcept = nullptr;
};

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The record of the unit of the baseline target, and of the unit of
// x86-64-v4.
extern TargetUnitBodies const baseline_unit_bodies;
extern TargetUnitBodies const wide_unit_bodies;

}  // namespace crucible::test
