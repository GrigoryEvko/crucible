// A lend refers to its count, and it steps the count at its end.  A lend of a
// temporary count would step an object that the full expression ended.

#include <foundation/core/LendCount.h>

#include <cstdint>

int main() {
    auto const lend = ::foundation::core::LendCount<std::uint32_t>{}.lend();
    static_cast<void>(lend);
    return 0;
}
