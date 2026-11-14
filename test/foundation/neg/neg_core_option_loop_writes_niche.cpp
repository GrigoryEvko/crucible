// A write through a mutable loop over an Option of an Id could write the
// highest value of the word, which is the empty value of the niche of an Id.
// The Option would then read as empty, with no fatal.  So the loop gives only
// the const cursor, and a mutable binding of the payload is refused.

#include <foundation/core/Id.h>

#include <cstdint>

namespace {

struct SlotTag {};

}  // namespace

int main() {
    using SlotId = ::foundation::core::Id<SlotTag, std::uint32_t>;
    auto held = ::foundation::core::Option<SlotId>::some(SlotId{5});
    for (SlotId& slot : held) {
        slot = SlotId{7};
    }
    return held.is_some() ? 0 : 1;
}
