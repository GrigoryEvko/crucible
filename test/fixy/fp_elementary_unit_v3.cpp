// The unit of test_fp_elementary for the level x86-64-v3.  The build compiles
// this file with -march=x86-64-v3, so each fp::fma is the instruction.

#include "fp_elementary_forms.h"

namespace test_fp_elementary {

FpDigests fp_digests_of_v3_unit() noexcept { return fp_digests_of_this_unit<FpUnit::V3>(); }

}  // namespace test_fp_elementary
