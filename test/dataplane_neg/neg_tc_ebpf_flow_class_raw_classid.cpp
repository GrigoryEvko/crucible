// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A flow class takes an admitted class id.  A raw integer does not convert
// to the refined class id, so the mint refuses it.  The DSCP and the
// priority are admitted, so the class id is the one thing refused.

#include <crucible/cntp/dataplane/TcEbpf.h>

#include <cstdint>

int main() {
    namespace dataplane = crucible::cntp::dataplane;
    auto const cls =
        dataplane::mint_tc_flow_class(dataplane::admit_tc_dscp(46).expect("46 is a DSCP value"), std::uint32_t{0x10001},
                                      dataplane::admit_tc_flow_priority(5).expect("5 is a priority"));
    return cls.value().classid.value() == 0x10001u ? 0 : 1;
}
