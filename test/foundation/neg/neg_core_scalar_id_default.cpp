// An Id has no default constructor: a field that can hold no id is an
// Option of the Id, so an Id with no value does not compile.

#include <foundation/core/Id.h>

#include <cstdint>

using SlotId = ::foundation::core::Id<struct SlotTag, std::uint32_t>;

int main() {
    SlotId const slot;
    return slot.raw() == 0u ? 0 : 1;
}
