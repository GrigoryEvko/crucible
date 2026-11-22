// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A counter of one writer is a cell of its value, so it is Synchronized, and
// run_joined admits two bodies that borrow it.  Each body publishes a step
// from the value that it read.  The publish of the one writer takes the proof
// of that writer (THE PROOF OF THE ONE WRITER of foundation/SoleWriterProof.h),
// and neither body holds one, so neither call has an overload.
//
// Expected diagnostic: no matching function for advance_sole_writer_from.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/os/ThreadTasks.h>

#include <foundation/effects/Ctx.h>

#include <cstdint>

struct CounterOwner {
    struct Role {};
};

int main() {
    auto counter =
        ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::SoleWriterProof<CounterOwner, CounterOwner::Role>>(0);
    auto first = [&counter]() noexcept {
        std::uint64_t const current = counter.get();
        counter.advance_sole_writer_from(current, current + 1);
    };
    auto second = first;
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    [[maybe_unused]] auto joined = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, first, second);
    return 0;
}
