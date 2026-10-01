// The generated family of four roles at depth 4 of
// test_session_global_attack.

#include "session_global_attack.h"

#include <utility>

namespace test_session_global_attack {

void check_four_roles(FamilyCounts& counts) {
    check_family<4, 4, 5000, false>(counts, four_roles_family, std::make_index_sequence<48>{});
}

}  // namespace test_session_global_attack
