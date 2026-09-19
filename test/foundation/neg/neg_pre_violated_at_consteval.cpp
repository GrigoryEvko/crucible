// CRUCIBLE_PRE on a struct taken by const reference fires during
// constant evaluation.  That is the parameter shape a native pre()
// clause skips.  The trap under `if consteval` is not a constant
// expression, so a violated predicate stops the static_assert below
// from evaluating.

#include <foundation/contracts/Pre.h>

#include <cstdint>

namespace {

struct S {
    std::uint64_t lo = 0;
    [[nodiscard]] constexpr bool nz() const noexcept { return lo != 0; }
};

[[nodiscard]] constexpr std::uint64_t under_test(S const& s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

// A default-constructed S has lo == 0, which violates the precondition.
constexpr S ZERO{};

static_assert(under_test(ZERO) == 0);

}  // namespace

int main() { return 0; }
