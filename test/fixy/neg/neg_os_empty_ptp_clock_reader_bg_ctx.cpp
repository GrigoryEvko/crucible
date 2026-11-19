// The reader of no device is for a test, so only a context that owns the
// Test capability gets one.  A background context that may open files owns
// no Test, so the mint refuses it, and a worker cannot stand in an empty
// reader for a clock that failed to open.
//
// The function takes the context by reference.  The failure is the mint's
// constraint, at the call.

#include <fixy/os/Time.h>

namespace eff = foundation::effects;

namespace {

using BackgroundFsCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block, eff::Effect::Nondet>>;

[[maybe_unused]] void attempt(BackgroundFsCtx const& background) {
    [[maybe_unused]] auto reader = fixy::time::mint_empty_ptp_clock_reader(background);
}

}  // namespace

int main() { return 0; }
