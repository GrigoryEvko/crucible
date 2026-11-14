// mint_heap_array_zeroed writes zero into each byte, so each bit pattern of
// the element must be a value.  No enumerator of Mode has the value zero, so
// a zero byte is no value of Mode, and the gate refuses the element.

#include <cstdint>

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

enum class Mode : std::uint8_t {
    first = 1,
    second = 2
};

int main() {
    auto modes = ::foundation::core::mint_heap_array_zeroed<Mode>(::foundation::effects::testing::test().alloc, 4);
    return static_cast<int>(modes.size());
}
