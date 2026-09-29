// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The in-process rule table holds at least one slot, so the table shape
// refuses a table of zero rules.

#include <crucible/cntp/Tcam.h>

int main() { return static_cast<int>(sizeof(crucible::cntp::tcam::TcamRules<0>)); }
