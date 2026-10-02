// Half 0 of the generated family of three roles at depth 4 of
// test_session_global_attack (session_global_attack.h gives the halves).

#include "session_global_attack.h"

namespace test_session_global_attack {

void check_three_roles_half_0(FamilyCounts& counts) {
    check_depth_four_half<3, three_roles_base, 0>(counts, three_roles_family);
}

}  // namespace test_session_global_attack
