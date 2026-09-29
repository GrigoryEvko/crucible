// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The Linear mint builds its value in place, and a rule handle has no
// public constructor from a slot and a generation.  So the mint cannot
// forge an owned rule either, and its constraint refuses the call.

#include <crucible/cntp/Tcam.h>
#include <fixy/Qtt.h>

#include <cstdint>

int main() {
    namespace tcam = crucible::cntp::tcam;

    auto forged = ::fixy::mint_linear<tcam::TcamRuleHandle>(
        crucible::cog::Uuid{1, 2}, tcam::admit_tcam_rule_id(1).value(), std::uint32_t{0}, std::uint32_t{1});
    (void)forged;
    return 0;
}
