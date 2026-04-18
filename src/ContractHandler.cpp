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
[[gnu::weak, noreturn]]
void handle_contract_violation(
    const std::contracts::contract_violation& v) noexcept {
    std::fprintf(stderr, "crucible: contract violation: %s\n", v.comment());
    std::abort();
}
