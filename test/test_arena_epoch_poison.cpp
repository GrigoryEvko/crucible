// A read through a pointer from an ended epoch.
//
// The end of an epoch poisons each block of the arena in a build with
// AddressSanitizer.  This program writes a value in an epoch, ends the
// epoch, and reads the value again.  The read must end the process with a
// report of a use after poison.  test/CMakeLists.txt registers the program
// only in a build with AddressSanitizer, and the test passes only on the
// text of that report.

#include <crucible/Arena.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

int main() {
    auto test = foundation::effects::testing::test();
    crucible::Arena arena{4096};

    crucible::ArenaEpoch epoch = arena.open_epoch();
    const fixy::WeakRef<uint64_t> value{*arena.alloc_obj<uint64_t>(test.alloc)};
    value.get() = 0x5EED;
    std::move(epoch).end();

    // The epoch ended, so this read must not return.
    ::fixy::report(::fixy::Sink::Out, "the read after the end of the epoch gave {}\n",
                   static_cast<unsigned long long>(value.get()));
    return 0;
}
