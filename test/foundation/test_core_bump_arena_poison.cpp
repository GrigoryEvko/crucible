// A read through a View of an ended epoch.
//
// The end of an epoch poisons each block of the arena in a build with
// AddressSanitizer.  This program writes a value in an epoch, ends the epoch,
// and reads the value again.  The read must end the process with a report of
// a use after poison.  test/foundation/CMakeLists.txt registers the program
// only in a build with AddressSanitizer, and the test passes only on the text
// of that report.

#include <foundation/core/BumpArena.h>

#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

int main() {
    ::foundation::core::BumpArena arena{4096};
    ::foundation::core::View<std::uint64_t> const value = arena.with_epoch(
        [&]() noexcept { return arena.array<std::uint64_t>(::foundation::effects::Alloc{}, 1, 0x5EED); });

    // The epoch ended, so this read must not return.
    std::uint64_t total = 0;
    for (std::uint64_t const element : value) {
        total += element;
    }
    ::foundation::core::report(::foundation::core::Sink::Out, "the read after the end of the epoch gave {}\n", total);
    return 0;
}
