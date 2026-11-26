// The unit of test_fp_elementary for the level x86-64 (v1).  The build
// compiles this file with -march=x86-64, so each fp::fma is the exact integer
// form.

#include "fp_elementary_forms.h"

namespace test_fp_elementary {

FpDigests fp_digests_of_v1_unit() noexcept { return fp_digests_of_this_unit<FpUnit::V1>(); }

}  // namespace test_fp_elementary
