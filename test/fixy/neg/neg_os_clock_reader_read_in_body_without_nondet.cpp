// A reader that a reference takes into a body whose context holds no Nondet
// does not read there.  The read takes the bare tag of Nondet, and the drain
// context of the background thread, which holds Bg and Alloc, gives no tag.
// The mint read the frame of the caller, but the gate of the read is at the
// call.
//
// Expected diagnostic: no candidate of extract_bare_from_ctx for the drain
// context, with a note on the row.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Capability.h>

namespace {

[[maybe_unused]] void observe(fixy::BgDrainCtx const& drain, fixy::time::MonotonicClock const& clock) {
    [[maybe_unused]] auto reading =
        clock.read(foundation::effects::extract_bare_from_ctx<foundation::effects::Effect::Nondet>(drain));
}

}  // namespace

int main() { return 0; }
