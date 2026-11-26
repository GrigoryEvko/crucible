// A read of a value of an ended epoch.
//
// The end of an epoch poisons each block of the arena in a build with
// AddressSanitizer.  This program writes a value in an epoch, ends the epoch,
// and reads the value again.  The read must end the process with a report of
// a use after poison.  test/foundation/CMakeLists.txt registers the program
// only in a build with AddressSanitizer, and the test passes only on the text
// of that report.
//
// The gate of with_epoch keeps each borrow of the epoch inside the epoch: the
// result holds no borrow, and no capture keeps a Ref, a run or a View of the
// epoch.  So the address of the value leaves the epoch as a number, which is
// a probe of raw memory, in an opt-out region.  The run-time check of the
// poison covers such a route, which no type can see.

#include <foundation/core/BumpArena.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

// The owner of the epoch arena of the program.
struct Scratch {
    struct[[= ::foundation::brand::arena_member_brand{}]] Arena final {};

    ::foundation::core::EpochArena<Arena> arena = ::foundation::core::mint_epoch_arena<Arena>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
};

}  // namespace

int main() {
    Scratch scratch{};

    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a value of an epoch leaves the epoch as a number")
    std::uintptr_t const address = scratch.arena.with_epoch(
        [](::foundation::core::Epoch<::foundation::core::EpochOf<Scratch::Arena>>& epoch) noexcept {
            auto const value =
                epoch.make<std::uint64_t>(::foundation::effects::testing::test().alloc, std::uint64_t{0x5EED});
            return __builtin_bit_cast(std::uintptr_t, __builtin_addressof(*value));
        });
    std::uint64_t const volatile* const ended = __builtin_bit_cast(std::uint64_t const volatile*, address);
    // The epoch ended, so this read must not return.
    std::uint64_t const read = *ended;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    ::foundation::core::report(::foundation::core::Sink::Out, "the read after the end of the epoch gave {}\n", read);
    return 0;
}
