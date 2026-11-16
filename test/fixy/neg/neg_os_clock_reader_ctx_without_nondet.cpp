// A clock gives a value that changes from run to run, so a reader is
// minted only by a context whose row holds Nondet.  The drain context of
// the background thread holds Bg and Alloc and no Nondet.  The atom of the
// thread is not the evidence: a background stage with no Nondet proves
// that its result does not depend on the time.
//
// The context is taken by reference so that nothing here has to build
// one.  The failure is the constraint of the mint, at the call.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

namespace {

[[maybe_unused]] void attempt(fixy::BgDrainCtx const& drain) {
    [[maybe_unused]] auto reader = fixy::time::mint_clock_reader<fixy::ClockSource_v::Monotonic>(drain);
}

}  // namespace

int main() { return 0; }
