// Part 0 of the generated pairs of the closure under duality: the pairs
// whose left protocol has an index from closure_part_begin(0) to
// closure_part_begin(1).

#include "session_subtype.h"

namespace test_session_subtype_types {

inline constexpr law_counts closure_counts_0 = check_pair_closure(0);

static_assert(closure_counts_0.pairs == (closure_part_begin(1) - closure_part_begin(0)) * generated_protocols);
static_assert(closure_counts_0.pair_closure == closure_counts_0.pairs,
              "on every generated pair, T refines U up to exits exactly when the dual of U refines the dual of T up "
              "to exits");

law_counts closure_part_0() { return closure_counts_0; }

}  // namespace test_session_subtype_types
