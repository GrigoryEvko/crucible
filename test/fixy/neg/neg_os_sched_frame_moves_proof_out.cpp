// The body of a scheduling frame gives the proof back by value.  The proof
// belongs to the thread that set the nice value, so it neither copies nor
// moves, and it exists only inside the frame of the mint.  The copy that the
// return of the body needs is deleted.

#include <fixy/Ctx.h>
#include <fixy/os/Sched.h>

#include <utility>

namespace {

[[maybe_unused]] void attempt(::fixy::BgDrainCtx const& ctx) {
    [[maybe_unused]] auto refused =
        fixy::sched::mint_priority<5>(ctx, [](auto proof) noexcept { return std::move(*proof); });
}

}  // namespace

int main() { return 0; }
