#pragma once

// The abort door of the base: the one call that ends the process with the
// signal SIGABRT.
//
// abort_process calls abort of the C library.  The C library owns the signals
// of the process.  For that reason, the base cannot do the steps of an abort
// with the raw primitive of foundation/os/Syscall.h:
//   - The deny rows of utils/rule-table.txt refuse a raw call of
//     rt_sigprocmask (the unblock of SIGABRT), tgkill (the signal to the
//     thread), rt_sigaction (the default action) and exit_group (the last
//     exit).
//   - Rule R20 of misc/01_10_2026_quarantine.md refuses a change of the
//     disposition of SIGABRT in a door.
// abort unblocks SIGABRT and sends it to the thread of the call.  When a
// handler of SIGABRT returns, abort puts back the default action, sends the
// signal again and ends the process.  A handler that does not return, such as
// the abort probe of the tests, gets the signal on the thread of the call.
//
// The door file src/foundation/os/Abort.cpp calls __builtin_abort, and it
// includes no header of the C library.  The rule artifact-symbols admits the
// import abort of libfoundation.a only from the member of that file (rule R5).
// Each path of the base that ends the process ends here: the contract
// handler, the fatal exit of the Report family, the failure arm of the
// assertion macros and the cold aborts of foundation.
//
// This header is in the layer F0_doors of utils/rule-table.txt.

namespace foundation::os {

// Ends the process with SIGABRT.  The function does not return.
[[noreturn, gnu::cold]] void abort_process() noexcept;

}  // namespace foundation::os
