// A read through a View of an ended epoch.
//
// The end of an epoch poisons each block of the arena in a build with
// AddressSanitizer.  This program writes a value in an epoch, ends the epoch,
// and reads the value again.  The read must end the process with a report of
// a use after poison.  test/foundation/CMakeLists.txt registers the program
// only in a build with AddressSanitizer, and the test passes only on the text
// of that report.
//
// The result of an epoch holds no borrow, and a capture by reference of a
// type that can hold the epoch brand is refused.  A plain View carries no
// brand, so the View leaves the epoch through a variable that the body
// captures.  That route is the one that the run-time check of the poison
// covers.

#include <foundation/core/BumpArena.h>

#include <foundation/core/Region.h>
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
    ::foundation::core::View<std::uint64_t const> value{};
    scratch.arena.with_epoch(
        [&value](::foundation::core::Epoch<::foundation::core::EpochOf<Scratch::Arena>>& epoch) noexcept {
            auto const run = epoch.array<std::uint64_t>(::foundation::effects::testing::test().alloc, 1, 0x5EED);
            value = run.view();
        });

    // The epoch ended, so this read must not return.
    std::uint64_t total = 0;
    for (std::uint64_t const element : value) {
        total += element;
    }
    ::foundation::core::report(::foundation::core::Sink::Out, "the read after the end of the epoch gave {}\n", total);
    return 0;
}
