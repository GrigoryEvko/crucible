// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-Checked fixture #2: safe_capacity<A, B> via the fixy::
// alias rejects an overflowing product with the named diagnostic
// [Checked_Capacity_Overflow].
//
// Violation: safe_capacity<A, B> is constexpr-guarded — it routes
// through checked_mul<size_t> and static_asserts on overflow.
// Multiplying two size_t-max values triggers the static_assert.
//
// Expected diagnostic: substring "Checked_Capacity_Overflow" or
// "safe_capacity<A, B>" / "overflow".

#include <crucible/fixy/Struct.h>

#include <cstddef>
#include <limits>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_checked_safe_capacity_overflow {

struct TypeStructCheckedSafeCapacityOverflow {};

// Multiply two near-max values; the product overflows size_t.
inline constexpr std::size_t kBigA = (std::size_t{1} << 40);
inline constexpr std::size_t kBigB = (std::size_t{1} << 40);

}  // namespace neg_fixy_struct_checked_safe_capacity_overflow

int main() {
    namespace tags = neg_fixy_struct_checked_safe_capacity_overflow;

    // Should FAIL: 2^40 * 2^40 = 2^80 overflows size_t.
    [[maybe_unused]] constexpr std::size_t bad =
        fstr::safe_capacity<tags::kBigA, tags::kBigB>;
    return 0;
}
