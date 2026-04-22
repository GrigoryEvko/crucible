#include <crucible/Platform.h>

#include <contracts>
#include <cstdio>
#include <cstdlib>

extern "C++"
[[gnu::weak, noreturn]]
void handle_contract_violation(
    const std::contracts::contract_violation& v) noexcept;

// Weak default: test binaries, benches, and vessel consumers pick this up
// automatically.  Production Keeper/Vessel override with a strong symbol
// that routes into crucible_abort() for coordinated teardown.
//
// Pause under an attached debugger via the crucible::detail shim for
// P2546R5 (libstdc++ 16.0.1 declares but doesn't yet ship the std::*
// definitions — see Platform.h).  Operator lands at the violation
// site with the contract_violation object still live on the stack.
// On unattended CI the pause no-ops and execution falls straight
// through to abort, which produces the core dump CI post-mortem
// tooling expects.
[[gnu::weak, noreturn]]
void handle_contract_violation(
    const std::contracts::contract_violation& v) noexcept {
    std::fprintf(stderr, "crucible: contract violation: %s\n", v.comment());
    ::crucible::detail::breakpoint_if_debugging();
    std::abort();
}
