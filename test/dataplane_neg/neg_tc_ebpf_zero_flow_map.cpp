// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A flow class map holds at least one flow, so the map shape refuses a map
// of zero flows.

#include <crucible/cntp/dataplane/TcEbpf.h>

int main() { return static_cast<int>(sizeof(crucible::cntp::dataplane::TcFlowClassMap<0>)); }
