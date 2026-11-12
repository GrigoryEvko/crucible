// An Id has no arithmetic: the sum of two ids does not compile.

#include <foundation/core/Id.h>

#include <cstdint>

using SlotId = ::foundation::core::Id<struct SlotTag, std::uint32_t>;

int main() {
    SlotId const first{1};
    SlotId const second{2};
    return (first + second).raw() == 3u ? 0 : 1;
}
