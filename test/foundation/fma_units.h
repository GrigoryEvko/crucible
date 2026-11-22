#pragma once

// The two other units of test_core_fma.  On x86_64 the build compiles
// fma_unit_wide.cpp for x86-64-v3, so fp::fma there is the fused
// multiply-add instruction (the FMA of foundation/core/Float.h).  It compiles
// fma_unit_baseline.cpp for x86-64, so fp::fma there is the exact form in
// integers, the fp::fma of a build at x86-64 or x86-64-v2.  The unit of the
// test takes the target of its preset: x86-64-v3 in the build of the preset
// default.  Each unit keeps its own body, because fp::fma is in the inline
// namespace of each target.  On aarch64 the three units run the instruction.

namespace crucible::test {

[[nodiscard]] float fma_of_wide_unit(float left, float right, float addend) noexcept;
[[nodiscard]] double fma_of_wide_unit(double left, double right, double addend) noexcept;

// True when the wide unit runs the instruction: its target has FMA.  The
// body reads only a macro, so a host of a lower level can call it.
[[nodiscard]] bool is_wide_unit_fused() noexcept;

[[nodiscard]] float fma_of_baseline_unit(float left, float right, float addend) noexcept;
[[nodiscard]] double fma_of_baseline_unit(double left, double right, double addend) noexcept;

// True when the baseline unit runs the exact form: its target has no FMA.
[[nodiscard]] bool is_baseline_unit_exact() noexcept;

}  // namespace crucible::test
