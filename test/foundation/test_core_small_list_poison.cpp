// A read of the spill storage of a SmallList past its count.
//
// In a build with AddressSanitizer, a SmallList poisons each slot of its heap
// storage at or past its count.  This program appends past the inline slots,
// so the elements move to the heap, keeps their View through a variable that
// the body of the frame door captures, takes the last element and reads it
// again through the View.  The read must end the process with a report of a
// use after poison.  test/foundation/CMakeLists.txt registers the program
// only in a build with AddressSanitizer, and the test passes only on the text
// of that report.

#include <foundation/core/SmallList.h>

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
    ::foundation::core::View<std::uint64_t const> kept{};
    list.with_view([&kept](::foundation::core::View<std::uint64_t const> elements) noexcept { kept = elements; });
    static_cast<void>(list.take_last().expect("the list holds three elements"));

    // The last slot is past the count, so this read must not return.
    std::uint64_t total = 0;
    for (std::uint64_t const element : kept) {
        total += element;
    }
    ::foundation::core::report(::foundation::core::Sink::Out, "the read past the count gave {}\n", total);
    return 0;
}
