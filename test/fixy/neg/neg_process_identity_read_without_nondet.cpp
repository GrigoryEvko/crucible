// The id of a process changes from run to run, so its read lifts Nondet as
// well as IO.  The compile context of the background thread holds Bg, Alloc
// and IO and no Nondet, so the identity door refuses it for Nondet alone.
//
// Expected diagnostic: no candidate of current_process for the compile
// context, with a note on the gate of the identity read.

#include <fixy/Ctx.h>
#include <fixy/os/Process.h>

namespace {

[[maybe_unused]] void attempt(fixy::BgCompileCtx const& compile) {
    [[maybe_unused]] auto const id = fixy::process::current_process(compile);
}

}  // namespace

int main() { return 0; }
