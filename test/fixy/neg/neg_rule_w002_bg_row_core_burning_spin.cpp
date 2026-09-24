// W002: Row<Bg> x a spin that burns the core.
//
// A spin is the right wait in the foreground, where the event is
// imminent and the alternative is a 1-5 us kernel round trip.  In the
// background it is the wrong one: the body holds a core the scheduler
// could have given to foreground work, and the event it waits for is by
// definition not imminent or it would not be in the background.
//
// The rule reads burns_the_core rather than "not a kernel wait", which is
// narrower by exactly one grade.  atom::sync::umwait_c01 halts the core
// in C0.1 instead of spinning it, so a background body may wait that way
// and this rule stands down for it — test/fixy/test_collision.cpp holds
// that cell beside this one.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with<::foundation::effects::Effect::Bg>,
                                ::fixy::atom::sync::spin_pause>
        refused{};
    return 0;
}
