// A BitSet takes the key type of its domain.  An argument position of a
// schema is no scalar slot of an op, so a set of scalar slots refuses it.

#include <foundation/core/BitSet.h>

#include <cstdint>

enum class ScalarSlot : std::uint8_t {
    first,
    second,
    third,
    fourth,
    fifth
};
enum class ArgumentPosition : std::uint8_t {
    receiver,
    other,
    alpha
};

int main() {
    ::foundation::core::BitSet<5, ScalarSlot> slots{};
    slots.set(ArgumentPosition::alpha);
    return static_cast<int>(slots.size());
}
