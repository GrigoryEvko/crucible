// The generated global types of test_session_global_attack.
//
// A fixed-seed generator builds global types over four roles and three
// labels.  Each type that the gates accept must give a live context at
// each capacity.  Most generated types are refused, and the counts are
// printed.  This file runs the families and holds the family of three
// roles.  The family of four roles is in
// test_session_global_attack_four_roles.cpp, and the parts of the family
// of two roles are in test_session_global_attack_two_roles_<k>.cpp.

#include "session_global_attack.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace test_session_global_attack {

namespace {

// Print the counts of one generated family.  Require a live type, and no
// type that projects onto every role but is not balanced.
void report_family(std::string_view family, const FamilyCounts& counts) {
    std::printf("  %-34.*s generated=%zu well_formed=%zu balanced_plus=%zu live=%zu variants_live=%zu/%zu "
                "projectable_but_unbalanced=%zu\n",
                static_cast<int>(family.size()), family.data(), counts.generated, counts.well_formed,
                counts.balanced_plus, counts.live_by_construction, counts.variants_live, counts.variants,
                counts.projectable_but_unbalanced);
    expect(counts.live_by_construction > 0, std::string{family} + ": the generator produced no live type");
    expect(counts.projectable_but_unbalanced == 0,
           std::string{family} + ": a type projects onto every role but is not balanced");
}

}  // namespace

void run_generated() {
    std::printf("generated global types (fixed seeds)\n");
    constexpr std::string_view three_roles_family = "three roles, depth 4";
    FamilyCounts three_roles;
    check_family<3, 4, 1000, false>(three_roles, three_roles_family, std::make_index_sequence<48>{});
    report_family(three_roles_family, three_roles);
    FamilyCounts four_roles;
    check_four_roles(four_roles);
    report_family(four_roles_family, four_roles);
    FamilyCounts two_roles;
    check_two_roles_part_0(two_roles);
    check_two_roles_part_1(two_roles);
    check_two_roles_part_2(two_roles);
    check_two_roles_part_3(two_roles);
    check_two_roles_part_4(two_roles);
    check_two_roles_part_5(two_roles);
    check_two_roles_part_6(two_roles);
    report_family(two_roles_family, two_roles);
}

// The parts of the family of two roles cover its 24 seeds, in order.
static_assert(two_roles_cuts.front() == 0 && two_roles_cuts.back() == 24);

}  // namespace test_session_global_attack
