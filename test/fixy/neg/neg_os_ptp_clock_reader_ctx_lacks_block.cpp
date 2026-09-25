// A background context can read a clock.  But the PTP reader also opens
// /dev/ptpN, and that open is a file system call that can hold the
// caller.  This context owns Bg and IO, and it does not own Block.  The
// clock half of the gate admits it, and the file system half refuses it.
// The mint's requires-clause is the whole gate: the reader's constructor
// is private, and the mint is its only friend.
//
// The function takes the context by reference and the index by value.
// Nothing here then builds either one.  The failure is the mint's
// constraint, at the call.

#include <fixy/os/Time.h>

namespace eff = foundation::effects;

namespace {

using BackgroundIoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

[[maybe_unused]] void attempt(BackgroundIoCtx const& background, fixy::time::PtpDeviceIndex index) {
    [[maybe_unused]] auto reader = fixy::time::mint_ptp_clock_reader(background, index);
}

}  // namespace

int main() { return 0; }
