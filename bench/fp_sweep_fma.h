#pragma once

// The sweep of fp::fma of a target with no fused multiply-add (the forms in
// doubles of foundation/core/Float.h) against two oracles: the exact form in
// integers, and the instruction.
//
//   sweep_fma_float(count)    count rounds of the float classes
//   sweep_fma_double(count)   count rounds of the double classes
//
// bench/fp_sweep_fma.cpp draws the inputs and holds the two oracles.  The
// unit bench/fp_sweep_fma_baseline.cpp compiles for x86-64, which has no
// fused multiply-add, so its fp::fma is the form in doubles.  The unit of the
// sweep compiles for the host, so its fp::fma is the instruction.  Each
// sweep returns 0 when each result agrees with the two oracles, and 1 when
// one does not or when the baseline unit does not run the form in doubles.

#include <cstdint>

namespace fp_sweep {

// fp::fma of the baseline unit: the form in doubles.
[[nodiscard]] float fma_of_baseline_unit(float left, float right, float addend) noexcept;
[[nodiscard]] double fma_of_baseline_unit(double left, double right, double addend) noexcept;

// True when the baseline unit has no fused multiply-add, so it runs the
// form in doubles.
[[nodiscard]] bool is_baseline_unit_unfused() noexcept;

[[nodiscard]] int sweep_fma_float(std::uint64_t count) noexcept;
[[nodiscard]] int sweep_fma_double(std::uint64_t count) noexcept;

}  // namespace fp_sweep
