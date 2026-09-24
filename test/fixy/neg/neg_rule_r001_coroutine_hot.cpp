// R001: coroutine x hot.
//
// A coroutine frame costs an indirect call plus a state spill at every
// suspension point, and the frame itself is heap-allocated unless the
// compiler elides it.  One resume breaches a budget measured in tens of
// nanoseconds, which is why CLAUDE.md III bans coroutines on the hot path
// outright rather than bounding them.
//
// The cost and refinement atoms are in the pack so the fixture trips
// R001 alone.  R002 and R003 need a borrow or a Bg row and stand down.

#include <fixy/Fn.h>

namespace {

struct resume_count_proved final {};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::coroutine, ::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                                ::fixy::atom::refined_with<resume_count_proved>>
        refused{};
    return 0;
}
