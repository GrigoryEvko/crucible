// Negative compile test.  This file must fail to compile.
//
// Once::call runs its argument, so the argument must be callable with no
// arguments.  The requires-clause of call refuses one that is not, at the
// call.  Without that check, the call passes the door and fails later,
// inside the body where the argument is called.  The regexes name the
// door, so they tell the two apart.
//
// Expected diagnostic: Once::call has no viable candidate, and the note
// names the is_invocable_v<F> check.

#include <fixy/Ctx.h>
#include <fixy/handle/Once.h>

namespace h = fixy::handle;

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    h::Once once{};
    once.call(test, 42);
    return 0;
}
