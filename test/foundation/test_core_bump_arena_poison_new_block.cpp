// A read past the bytes of an allocation, in a new block, before an epoch
// ends.
//
// A new block of the arena is poisoned in a build with AddressSanitizer, and
// each allocation unpoisons only the bytes that it gives.  This program takes
// one value from an arena with no epoch, so the value is the first allocation
// of a new block.  Then it reads the bytes after the value, which the arena
// did not give out.  The read must end the process with a report of a use
// after poison.  test/foundation/CMakeLists.txt registers the program only in
// a build with AddressSanitizer, and the test passes only on the text of that
// report.
//
// The arena gives out no address of the bytes after an allocation, so the
// read is a probe of raw memory, in an opt-out region.

#include <foundation/core/BumpArena.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

int main() {
    ::foundation::core::BumpArena arena{4096};
    auto const value = arena.make<std::uint64_t>(::foundation::effects::testing::test().alloc, std::uint64_t{0x5EED});

    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a read of the bytes after an allocation, which the arena did not give out")
    std::uint64_t const volatile* const after = __builtin_addressof(*value) + 1;
    // The arena gave out no byte after the value, so this read must not
    // return.
    std::uint64_t const read = *after;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    ::foundation::core::report(::foundation::core::Sink::Out, "the read after the allocation gave {}\n", read);
    return 0;
}
