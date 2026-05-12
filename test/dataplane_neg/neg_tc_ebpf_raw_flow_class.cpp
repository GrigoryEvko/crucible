#include <crucible/cntp/dataplane/TcEbpf.h>

int main() {
    crucible::cntp::dataplane::TcFlowClass raw{};
    crucible::cntp::dataplane::TcFlowClassMap<4> map{};
    auto key = crucible::cntp::dataplane::tc_flow_key(crucible::cntp::SocketFd{7});
    auto updated = map.update(key, raw);
    return updated.has_value() ? 0 : 1;
}
