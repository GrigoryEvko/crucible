// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A flow class takes an admitted DSCP.  A raw byte does not convert to the
// refined DSCP, so the mint refuses it.  The class id and the priority are
// admitted, so the DSCP is the one thing refused.

#include <crucible/cntp/dataplane/TcEbpf.h>

#include <cstdint>

int main() {
    namespace dataplane = crucible::cntp::dataplane;
    auto cls = dataplane::mint_tc_flow_class(std::uint8_t{46}, dataplane::admit_tc_classid(0x10001).value(),
                                             dataplane::admit_tc_flow_priority(5).value());
    (void)cls;
    return 0;
}
