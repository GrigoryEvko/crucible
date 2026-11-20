// A read of a slot of a SlotMap after its value left.
//
// In a build with AddressSanitizer, a SlotMap poisons each slot that holds no
// value: each slot at or past its used count, and each slot that remove
// freed.  This program inserts a value, keeps a Ref of it, removes the value
// and reads the Ref.  The read must end the process with a report of a use
// after poison.  test/foundation/CMakeLists.txt registers the program only in
// a build with AddressSanitizer, and the test passes only on the text of that
// report.

#include <foundation/core/SlotMap.h>

#include <foundation/core/Quantity.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

struct Probe final {};

}  // namespace

int main() {
    using Capacity = ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>;
    auto map = ::foundation::core::mint_slot_map<std::uint64_t, Probe>(::foundation::effects::testing::test().alloc,
                                                                       Capacity{8});
    auto const key = map.insert(0x5EED).expect("the map has a free slot");
    auto const kept = map.get(key).expect("the map holds the value of the key");
    static_cast<void>(map.remove(key).expect("the map holds the value of the key"));

    // The slot holds no value, so this read must not return.
    std::uint64_t const seen = *kept;
    ::foundation::core::report(::foundation::core::Sink::Out, "the read of a freed slot gave {}\n", seen);
    return 0;
}
