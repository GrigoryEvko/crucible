// The reader of no device is for a test, so only a context that owns the
// Test capability gets one.  An init context owns Init and no Test, so the
// mint refuses it, and start-up code cannot hold a PTP handle with no clock.
//
// The function takes the context by reference.  The failure is the mint's
// constraint, at the call.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

namespace {

[[maybe_unused]] void attempt(fixy::ColdInitCtx const& init) {
    [[maybe_unused]] auto reader = fixy::time::mint_empty_ptp_clock_reader(init);
}

}  // namespace

int main() { return 0; }
