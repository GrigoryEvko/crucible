// Negative compile test.  This file must fail to compile.
//
// The flow map takes a flow class that mint_tc_flow_class declared.  A raw
// class does not convert to the declared class, so the update is refused.

#include <crucible/cntp/dataplane/TcEbpf.h>

int main() {
    crucible::cntp::dataplane::TcFlowClass const raw{};
    crucible::cntp::dataplane::TcFlowClassMap<4> map{};
    crucible::cntp::dataplane::NetFlowKey const key{.src_port = 40000, .dst_port = 443, .proto = 6};
    return map.update(key, raw).is_ok() ? 0 : 1;
}
