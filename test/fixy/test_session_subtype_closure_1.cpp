// Part 1 of the generated pairs of the closure under duality: the pairs
// whose left protocol has an index from closure_part_begin(1) to
// closure_part_begin(2).

#include "session_subtype.h"

namespace test_session_subtype_types {

inline constexpr law_counts closure_counts_1 = check_pair_closure(1);

static_assert(closure_counts_1.pairs == (closure_part_begin(2) - closure_part_begin(1)) * generated_protocols);
static_assert(closure_counts_1.pair_closure == closure_counts_1.pairs,
              "on every generated pair, T refines U up to exits exactly when the dual of U refines the dual of T up "
              "to exits");

law_counts closure_part_1() { return closure_counts_1; }

}  // namespace test_session_subtype_types
