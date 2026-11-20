// A read of the storage of a GrowText past its count.
//
// In a build with AddressSanitizer, a GrowText poisons each character of its
// storage at or past its count.  This program appends a text, keeps its
// TextView through a variable that the body of the frame door captures,
// clears the text and reads the TextView.  The read must end the process
// with a report of a use after poison.  test/foundation/CMakeLists.txt
// registers the program only in a build with AddressSanitizer, and the test
// passes only on the text of that report.

#include <foundation/core/Grow.h>

#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto text = ::foundation::core::mint_grow_text(256);
    text.append(alloc, ::foundation::core::TextView{"poisoned"}).expect("the text has room");
    ::foundation::core::TextView kept{};
    text.with_view([&kept](::foundation::core::TextView characters) noexcept { kept = characters; });
    text.clear();

    // The count is zero, so this read must not return.
    std::size_t total = 0;
    for (char const character : kept) {
        total += static_cast<unsigned char>(character);
    }
    ::foundation::core::report(::foundation::core::Sink::Out, "the read past the count gave {}\n", total);
    return 0;
}
