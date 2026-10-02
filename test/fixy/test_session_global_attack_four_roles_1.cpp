// Half 1 of the generated family of four roles at depth 4 of
// test_session_global_attack (session_global_attack.h gives the halves).

#include "session_global_attack.h"

namespace test_session_global_attack {

void check_four_roles_half_1(FamilyCounts& counts) {
    check_depth_four_half<4, four_roles_base, 1>(counts, four_roles_family);
}

}  // namespace test_session_global_attack
