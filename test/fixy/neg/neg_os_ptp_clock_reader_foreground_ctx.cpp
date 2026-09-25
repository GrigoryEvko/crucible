// A PTP clock read on the replay-bound foreground path makes replay
// diverge across machines.  The open of /dev/ptpN is also a file system
// call that can hold the caller.  Only a context that owns Bg, Init or
// Test, and also owns IO and Block, can mint a PTP reader.  A foreground
// context owns none of them.  The mint's requires-clause is the whole
// gate: the reader's constructor is private, and the mint is its only
// friend.
//
// The function takes the context by reference and the index by value.
// Nothing here then builds either one.  The failure is the mint's
// constraint, at the call.

#include <fixy/os/Time.h>

namespace eff = foundation::effects;

namespace {

using ForegroundCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

[[maybe_unused]] void attempt(ForegroundCtx const& foreground, fixy::time::PtpDeviceIndex index) {
    [[maybe_unused]] auto reader = fixy::time::mint_ptp_clock_reader(foreground, index);
}

}  // namespace

int main() { return 0; }
