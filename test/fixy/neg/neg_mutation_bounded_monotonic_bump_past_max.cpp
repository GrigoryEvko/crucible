// The counter holds max().  A bump would move it past its bound.  The
// precondition of bump() stops the constant evaluation, so the
// static_assert below has no constant condition.

#include <fixy/Mutation.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr std::uint32_t bump_past_the_bound() {
    auto counter = ::fixy::mint_bounded_monotonic<std::uint32_t, 2U>(2U);
    counter.bump();
    return counter.get();
}

static_assert(bump_past_the_bound() == 3U);

}  // namespace

int main() { return 0; }
