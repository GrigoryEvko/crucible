// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A table plan comes only from mint_tcam_table, which checks the target and
// the capacity.  The tagged wrapper opens a default constructor only for a
// value that has one, and a plan has none, so a default plan is refused.

#include <crucible/cntp/Tcam.h>

int main() {
    crucible::cntp::tcam::DeclaredTcamTable plan{};
    return static_cast<int>(plan.value().capacity().value());
}
