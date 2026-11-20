// A read of the storage of a GrowList past its count.
//
// In a build with AddressSanitizer, a GrowList poisons each slot of its
// storage at or past its count.  This program appends two elements, so the
// storage holds a cache line of slots, and keeps a View of them that leaves
// the frame door through a variable that the body captures.  It then clears
// the list, so the count is zero, and reads the View.  The read must end the
// process with a report of a use after poison.  test/foundation/CMakeLists.txt
// registers the program only in a build with AddressSanitizer, and the test
// passes only on the text of that report.

#include <foundation/core/Grow.h>

#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_grow_list<std::uint64_t>(64);
    list.append(alloc, 0x5EED).expect("the list holds less than its maximum");
    list.append(alloc, 0x5EEE).expect("the list holds less than its maximum");
    ::foundation::core::View<std::uint64_t const> kept{};
    list.with_view([&kept](::foundation::core::View<std::uint64_t const> elements) noexcept { kept = elements; });
    list.clear();

    // The count is zero, so this read must not return.
    std::uint64_t total = 0;
    for (std::uint64_t const element : kept) {
        total += element;
    }
    ::foundation::core::report(::foundation::core::Sink::Out, "the read past the count gave {}\n", total);
    return 0;
}
