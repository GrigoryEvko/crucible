#include <crucible/cntp/dataplane/Xdp.h>

struct Key {
    std::uint32_t value = 0;
};

struct Value {
    std::uint32_t value = 0;
};

int main() {
    crucible::cntp::dataplane::BpfMapImage<Key, Value, 4> map{};
    return static_cast<int>(map.size());
}
