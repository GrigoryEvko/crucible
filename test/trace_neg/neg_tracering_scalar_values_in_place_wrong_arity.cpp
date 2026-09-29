// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TraceRing::Entry::scalar_values is fixy::FixedArray<int64_t, 5>.  The
// in-place constructor of FixedArray takes std::in_place and exactly N
// values: its clause `sizeof...(Args) == N` refuses a partial fill and an
// over-fill.  A partial fill would value-initialize the trailing slots and
// break the type tag of each slot in scalar_types and op_flags.
//
// This fixture passes std::in_place and six values to a five-slot array,
// so only the arity clause refuses it.  The companion fixture
// neg_tracering_scalar_values_at_oob.cpp holds the access side: at<I>()
// refuses an index that is not less than N.

#include <crucible/TraceRing.h>

#include <cstdint>
#include <utility>

int main() {
    using ScalarValues = decltype(crucible::TraceRing::Entry::scalar_values);
    ScalarValues bad{std::in_place, int64_t{1}, int64_t{2}, int64_t{3}, int64_t{4}, int64_t{5}, int64_t{6}};
    (void)bad;
    return 0;
}
