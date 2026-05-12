#include <crucible/rt/TcEbpf.h>

int main() {
    crucible::rt::TcFlowClassMap<0> map{};
    return static_cast<int>(map.size());
}
