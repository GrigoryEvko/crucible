// A read of the spill storage of a SmallList past its count.
//
// In a build with AddressSanitizer, a SmallList poisons each slot of its heap
// storage at or past its count.  This program appends past the inline slots,
// so the elements move to the heap.  The body of the frame door keeps the
// address of the first element as an integer, because the store check of
// the door refuses a body that keeps the View itself
// (neg_core_small_list_view_kept_past_its_frame).  The program then takes the
// last element and reads the three slots again through the address.  The
// read must end the process with a report of a use after poison.
// test/foundation/CMakeLists.txt registers the program only in a build with
// AddressSanitizer, and the test passes only on the text of that report.

#include <foundation/core/SmallList.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

int main() {
    using Maximum = ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>;
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_small_list<std::uint64_t, 2>(Maximum{64});
    for (std::uint64_t value = 1; value != 4; ++value) {
        list.append(alloc, value).expect("the list holds less than its maximum");
    }
    std::uint64_t total = 0;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a raw read past the count tests the poison under AddressSanitizer")
    std::uintptr_t first_address = 0;
    list.with_view([&first_address](::foundation::core::View<std::uint64_t const> elements) noexcept {
        first_address = __builtin_bit_cast(std::uintptr_t, &*elements.begin());
    });
    static_cast<void>(list.take_last().expect("the list holds three elements"));

    // The last slot is past the count, so this read must not return.
    std::uint64_t const* const stale = __builtin_bit_cast(std::uint64_t const*, first_address);
    for (std::size_t position = 0; position != 3; ++position) {
        total += stale[position];
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    ::foundation::core::report(::foundation::core::Sink::Out, "the read past the count gave {}\n", total);
    return 0;
}
