// A read of the storage of a GrowText past its count.
//
// In a build with AddressSanitizer, a GrowText poisons each character of its
// storage at or past its count.  This program appends a text.  The body of
// the frame door keeps the address of the first character as an integer,
// because the store check of the door refuses a body that keeps the TextView
// itself (neg_core_grow_text_view_kept_past_its_frame).  The program then
// clears the text and reads the characters through the address.  The read
// must end the process with a report of a use after poison.
// test/foundation/CMakeLists.txt registers the program only in a build with
// AddressSanitizer, and the test passes only on the text of that report.

#include <foundation/core/Grow.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto text = ::foundation::core::mint_grow_text(256);
    text.append(alloc, ::foundation::core::TextView{"poisoned"}).expect("the text has room");
    std::size_t total = 0;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a raw read past the count tests the poison under AddressSanitizer")
    std::uintptr_t first_address = 0;
    text.with_view([&first_address](::foundation::core::TextView characters) noexcept {
        first_address = __builtin_bit_cast(std::uintptr_t, &*characters.begin());
    });
    text.clear();

    // The count is zero, so this read must not return.
    char const* const stale = __builtin_bit_cast(char const*, first_address);
    for (std::size_t position = 0; position != 8; ++position) {
        total += static_cast<unsigned char>(stale[position]);
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    ::foundation::core::report(::foundation::core::Sink::Out, "the read past the count gave {}\n", total);
    return 0;
}
