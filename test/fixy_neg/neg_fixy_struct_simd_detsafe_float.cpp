// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-Simd fixture #1: DetSafeSimd<V> via the fixy:: alias
// rejects a floating-point-lane vec type per CLAUDE.md §II.8.
//
// Violation: DetSafeSimd<V> requires `std::integral<V::value_type>`;
// a facade vec<float, 8> is FP-lane and breaks BITEXACT recipes across
// ISAs (chunked-fold reordering).  Routing through
// `fixy::struct_::simd::DetSafeSimd` must reject identically.
//
// Expected diagnostic: "constraints not satisfied" pointing at the
// DetSafeSimd concept / std::integral.

#include <crucible/fixy/Struct.h>
#include <crucible/safety/Simd.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_simd_detsafe_float {

struct TypeStructSimdDetSafeFloat {};

template <typename V>
    requires fstr::simd::DetSafeSimd<V>
[[nodiscard]] constexpr int gate() noexcept { return 1; }

}  // namespace neg_fixy_struct_simd_detsafe_float

int main() {
    namespace tags = neg_fixy_struct_simd_detsafe_float;
    using FpVec = ::crucible::simd::vec<float, 8>;

    // Should FAIL: FpVec has float lanes; DetSafeSimd rejects.
    [[maybe_unused]] auto bad = tags::gate<FpVec>();
    return 0;
}
