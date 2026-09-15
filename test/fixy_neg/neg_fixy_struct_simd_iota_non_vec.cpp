// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-Simd fixture #2: iota_v<V> via the fixy:: alias rejects
// a non-vec type (V must have ::value_type and the generator-ctor
// shape).
//
// Violation: iota_v<V> constructs V via the per-lane generator
// constructor `V([](auto lane) ...)`; passing `int` fails because
// `int` has no `value_type`, no per-lane constructor, and no
// `operator()(auto)`.  Routing through
// `fixy::struct_::simd::iota_v` must reject identically.
//
// Expected diagnostic: the `iota_v<int>` instantiation context plus
// "'int' is not a class, struct, or union type" from the generator
// constructor.  GCC does NOT say "no type named 'value_type'" here —
// that phrase only ever appeared in the quoted source of `iota_v`, so
// the earlier gate matched the fixture's own text rather than any
// diagnostic.

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_simd_iota_non_vec {

struct TypeStructSimdIotaNonVec {};

}  // namespace neg_fixy_struct_simd_iota_non_vec

int main() {
    // Should FAIL: int has no ::value_type, no per-lane ctor.
    [[maybe_unused]] auto bad = fstr::simd::iota_v<int>();
    return 0;
}
