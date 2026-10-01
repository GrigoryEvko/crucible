// Part 2 of the generated pairs of the closure under duality: the pairs
// whose left protocol has an index from closure_part_begin(2) to
// closure_part_begin(3).

#include "session_subtype.h"

namespace test_session_subtype_types {

inline constexpr law_counts closure_counts_2 = check_pair_closure(2);

static_assert(closure_counts_2.pairs == (closure_part_begin(3) - closure_part_begin(2)) * generated_protocols);
static_assert(closure_counts_2.pair_closure == closure_counts_2.pairs,
              "on every generated pair, T refines U up to exits exactly when the dual of U refines the dual of T up "
              "to exits");

law_counts closure_part_2() { return closure_counts_2; }

}  // namespace test_session_subtype_types
