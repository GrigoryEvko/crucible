// The proof of the GIL of vessel/torch/gil_held.h, in a process with no
// Python interpreter.
//
//   - GilHeld does not copy, its assignments are deleted, and no code outside
//     mint_gil_held constructs it.  It moves, because an Option of it moves
//     it out of the mint.  Its attribute thread_bound keeps it from another
//     thread: it is not Sendable.
//   - This process links no libpython, so the weak symbol PyGILState_Check is
//     null.  mint_gil_held then gives none and calls nothing, which is the
//     arm that refuses each read of a PyTorch object when the symbol does not
//     resolve.  test_gil_entries.py of the vessel tests the other arms in the
//     Python process.

#include "../vessel/torch/gil_held.h"

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Effect.h>

#include <type_traits>

namespace {

using crucible::vessel::GilHeld;
using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

static_assert(!std::is_default_constructible_v<GilHeld>, "only mint_gil_held constructs the proof");
static_assert(!std::is_copy_constructible_v<GilHeld>);
static_assert(!std::is_copy_assignable_v<GilHeld>);
static_assert(!std::is_move_assignable_v<GilHeld>);
static_assert(std::is_nothrow_move_constructible_v<GilHeld>, "an Option of the proof moves it out of the mint");
static_assert(!::foundation::transfer::Sendable<GilHeld>, "the proof stays on the thread that holds the GIL");
static_assert(std::is_same_v<decltype(crucible::vessel::mint_gil_held()), ::fixy::Option<GilHeld>>);

void test_no_interpreter(TestRunnerCtx const& test) {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the test reads the address of the weak symbol of the interpreter")
    const bool is_symbol_null = PyGILState_Check == nullptr;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    CRUCIBLE_FATAL_INVARIANT(is_symbol_null);
    CRUCIBLE_FATAL_INVARIANT(crucible::vessel::mint_gil_held().is_none());
    pass(test, "  test_no_interpreter: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_no_interpreter(test);
    return 0;
}
