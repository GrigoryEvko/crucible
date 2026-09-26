// A carrier with a user-provided copy is not trivially copyable, so a
// compare-exchange over its bytes would skip the copy it defines.  The
// door refuses it as the carrier of an atomic counter.

#include <fixy/Mutation.h>

namespace {

struct Stamp {
    int value = 0;
    constexpr explicit Stamp(int v) noexcept : value{v} {}
    constexpr Stamp(Stamp const& other) noexcept : value{other.value} {}
    constexpr Stamp& operator=(Stamp const&) noexcept = default;
    constexpr bool operator<(Stamp const& other) const noexcept { return value < other.value; }
};

}  // namespace

int main() {
    auto counter = fixy::mint_atomic_monotonic<Stamp>(Stamp{1});
    return counter.get().value;
}
