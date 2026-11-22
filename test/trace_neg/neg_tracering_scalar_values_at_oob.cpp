// Negative-compile test.  This file must fail to compile.
//
// Entry::scalar_values of TraceRing is a fixy::FixedArray<int64_t, 5>.  Its
// at() takes an Index<5> or a constant index below 5.  The constant index 5
// converts to no Index<5>, so the read of the sixth slot does not compile.
//
// The companion fixture neg_tracering_scalar_values_list_wrong_arity
// holds the side of the construction: a list of six values for five slots.

#include <crucible/TraceRing.h>

int main() {
    crucible::TraceRing::Entry entry{};
    (void)entry.scalar_values.at(::fixy::index<5>());
    return 0;
}
