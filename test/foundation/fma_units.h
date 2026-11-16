#pragma once

// The second unit of test_core_fma.  On x86_64 the build compiles
// fma_unit_wide.cpp for x86-64-v3, so fp::fma there is the fused
// multiply-add instruction (THE FMA of foundation/core/Float.h).  The unit of
// the test takes the target of its preset, so with no -march its fp::fma is
// the exact form in integers.  The two units keep their own body, because
// fp::fma is in the inline namespace of each target.  On aarch64 the two
// units run the instruction.

namespace crucible::test {

[[nodiscard]] float fma_of_wide_unit(float left, float right, float addend) noexcept;
[[nodiscard]] double fma_of_wide_unit(double left, double right, double addend) noexcept;

// True when the wide unit runs the instruction: its target has FMA.  The
// body reads only a macro, so a host of a lower level can call it.
[[nodiscard]] bool is_wide_unit_fused() noexcept;

}  // namespace crucible::test
