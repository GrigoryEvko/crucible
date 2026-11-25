// A read of a segment of a SegmentList past its count.
//
// In a build with AddressSanitizer, a SegmentList poisons each slot of its
// last segment that holds no element.  This program appends two elements, so
// segment 0 has 30 free slots, and reads the first free slot through the
// address of the first element.  The read must end the process with a report
// of a use after poison.  test/foundation/CMakeLists.txt registers the program
// only in a build with AddressSanitizer, and the test passes only on the text
// of that report.

#include <foundation/core/SegmentList.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::SegmentList<std::uint64_t> list;
    list.append(alloc, 0x5EED);
    list.append(alloc, 0x5EEE);
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a raw read past the count tests the poison under AddressSanitizer")
    std::uint64_t const* const first = &*list.begin();

    // The count is two, so this read must not return.
    std::uint64_t const past = first[2];
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    ::foundation::core::report(::foundation::core::Sink::Out, "the read past the count gave {}\n", past);
    return 0;
}
