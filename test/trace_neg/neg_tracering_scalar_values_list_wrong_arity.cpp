// Negative-compile test.  This file must fail to compile.
//
// Entry::scalar_values of TraceRing is a fixy::FixedArray<int64_t, 5>.  Its
// list constructor takes exactly 5 values, so it refuses a partial fill and
// an over-fill.  A partial fill would leave the trailing slots with a value
// that no code chose, and break the type tag of each slot in scalar_types
// and op_flags.
//
// This fixture gives six values to a five-slot array, so only the count of
// the list refuses it.  The companion fixture
// neg_tracering_scalar_values_at_oob holds the side of the access: at()
// refuses a constant index that is not less than 5.

#include <crucible/TraceRing.h>

#include <cstdint>

int main() {
    using ScalarValues = decltype(crucible::TraceRing::Entry::scalar_values);
    ScalarValues bad{int64_t{1}, int64_t{2}, int64_t{3}, int64_t{4}, int64_t{5}, int64_t{6}};
    (void)bad;
    return 0;
}
