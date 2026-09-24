#include <crucible/cntp/dataplane/TcEbpf.h>

int main() {
    crucible::cntp::dataplane::TcFlowClassMap<0> map{};
    return static_cast<int>(map.size());
}
