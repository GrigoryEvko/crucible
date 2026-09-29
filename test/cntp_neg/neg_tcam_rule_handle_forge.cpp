// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A rule handle proves that one table installed one rule, so only the
// rule table builds one.  A handle built by hand from a slot and a
// generation that the caller guessed is refused.

#include <crucible/cntp/Tcam.h>

#include <cstdint>

int main() {
    namespace tcam = crucible::cntp::tcam;

    tcam::TcamRuleHandle forged{crucible::cog::Uuid{1, 2}, tcam::admit_tcam_rule_id(1).value(), std::uint32_t{0},
                                std::uint32_t{1}};
    return static_cast<int>(forged.slot());
}
