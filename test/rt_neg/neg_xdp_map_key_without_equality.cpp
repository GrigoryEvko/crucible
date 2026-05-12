#include <crucible/rt/Xdp.h>

struct Key {
    std::uint32_t value = 0;
};

struct Value {
    std::uint32_t value = 0;
};

int main() {
    crucible::rt::BpfMapImage<Key, Value, 4> map{};
    return static_cast<int>(map.size());
}
