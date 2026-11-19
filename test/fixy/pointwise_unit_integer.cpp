// A unit of test_pointwise: the properties of the add of 4-byte integers, of
// the negate of 8-byte integers and of the add of 2-byte integers on the
// scalar path.  pointwise_rig.h tells why the tests compile in more than one
// unit.

#include "pointwise_rig.h"

#include <cstdint>

namespace test_pointwise {

void test_the_integer_cases(::fixy::TestRunnerCtx const& test) noexcept {
    pointwise_agrees_with_walk<std::uint32_t, 2>(test, "uint32 add", kSeed + 6, Add{});
    pointwise_agrees_with_walk<std::int64_t, 1>(test, "int64 negate", kSeed + 9, Negate{});
    pointwise_agrees_with_walk<std::uint16_t, 2>(test, "uint16 add on the scalar path", kSeed + 8, Add{});
}

}  // namespace test_pointwise
