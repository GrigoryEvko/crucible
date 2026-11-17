// Each enumerator of the key of a BitSet lies below the count of its
// positions.  The fifth scalar slot has the position 4, so a set of four
// positions refuses the enum.

#include <foundation/core/BitSet.h>

#include <cstdint>

enum class ScalarSlot : std::uint8_t {
    first,
    second,
    third,
    fourth,
    fifth
};

int main() { return static_cast<int>(sizeof(::foundation::core::BitSet<4, ScalarSlot>)); }
