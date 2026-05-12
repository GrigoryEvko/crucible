#include <crucible/rt/TcEbpf.h>

int main() {
    crucible::rt::TcFlowClass raw{};
    crucible::rt::TcFlowClassMap<4> map{};
    auto key = crucible::rt::tc_flow_key(crucible::cntp::SocketFd{7});
    auto updated = map.update(key, raw);
    return updated.has_value() ? 0 : 1;
}
