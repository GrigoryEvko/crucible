// A unit of test_pointwise: the property of the multiply-add of 8-byte
// unsigned integers.  pointwise_rig.h tells why the tests compile in more
// than one unit.

#include "pointwise_rig.h"

#include <cstdint>

namespace test_pointwise {

void test_the_uint64_multiply_add(::fixy::TestRunnerCtx const& test) noexcept {
    // A shared input reads the outputs of earlier steps, so a chain of products
    // has no bound.  Thus the multiply-add of 8-byte integers is unsigned,
    // which wraps, and the signed lanes take the negate.
    pointwise_agrees_with_walk<std::uint64_t, 3>(test, "uint64 multiply-add", kSeed + 7, MultiplyAdd{});
}

}  // namespace test_pointwise
