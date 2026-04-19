// libstdc++ requires a user-provided handle_contract_violation when
// -fcontracts is on.
#include <contracts>
#include <cstdio>
#include <cstdlib>

extern "C++" [[noreturn]] void handle_contract_violation(
    const std::contracts::contract_violation& v) noexcept;

[[noreturn]] void handle_contract_violation(
    const std::contracts::contract_violation& v) noexcept {
    std::fprintf(stderr, "contract: %s\n", v.comment());
    std::abort();
}
