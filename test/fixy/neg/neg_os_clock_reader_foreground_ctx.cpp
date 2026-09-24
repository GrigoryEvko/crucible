// A clock read on the replay-bound foreground path makes replay diverge
// across machines, so a reader is minted only by a context that owns
// Bg, Init or Test.  A foreground context owns none of them, and the
// mint's requires-clause is the whole gate: the reader's constructor is
// private and the mint is its only friend, so there is no other door to
// try.
//
// The context is taken by reference so that nothing here has to build
// one.  The failure is the mint's constraint, at the call.

#include <fixy/os/Time.h>

namespace eff = foundation::effects;

namespace {

using ForegroundCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

[[maybe_unused]] void attempt(ForegroundCtx const& foreground) {
    [[maybe_unused]] auto reader = fixy::time::mint_clock_reader<fixy::ClockSource_v::Monotonic>(foreground);
}

}  // namespace

int main() { return 0; }
