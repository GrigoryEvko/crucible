// A read of the storage of a GrowList past its count.
//
// In a build with AddressSanitizer, a GrowList poisons each slot of its
// storage at or past its count.  This program appends two elements, so the
// storage holds a cache line of slots.  The body of the frame door keeps the
// address of the first element as an integer, because the store check of
// the door refuses a body that keeps the View itself
// (neg_core_grow_list_view_kept_past_its_frame).  The program then clears the
// list, so the count is zero, and reads the two slots through the address.
// The read must end the process with a report of a use after poison.
// test/foundation/CMakeLists.txt registers the program only in a build with
// AddressSanitizer, and the test passes only on the text of that report.

#include <foundation/core/Grow.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_grow_list<std::uint64_t>(64);
    list.append(alloc, 0x5EED).expect("the list holds less than its maximum");
    list.append(alloc, 0x5EEE).expect("the list holds less than its maximum");
    std::uint64_t total = 0;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a raw read past the count tests the poison under AddressSanitizer")
    std::uintptr_t first_address = 0;
    list.with_view([&first_address](::foundation::core::View<std::uint64_t const> elements) noexcept {
        first_address = __builtin_bit_cast(std::uintptr_t, &*elements.begin());
    });
    list.clear();

    // The count is zero, so this read must not return.
    std::uint64_t const* const stale = __builtin_bit_cast(std::uint64_t const*, first_address);
    for (std::size_t position = 0; position != 2; ++position) {
        total += stale[position];
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    ::foundation::core::report(::foundation::core::Sink::Out, "the read past the count gave {}\n", total);
    return 0;
}
