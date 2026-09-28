// ct::eq carries CRUCIBLE_PRE(a.size() == b.size()).  An empty span and
// a two-byte span reach the macro's consteval trap, so the static_assert
// below has a non-constant condition.  Without the precondition the loop
// runs over no bytes and eq answers true, so the fixture compiles.
#include <fixy/ConstantTime.h>

#include <cstddef>
#include <span>

namespace {
constexpr std::byte two_bytes[2]{std::byte{0x42}, std::byte{0x43}};
static_assert(::fixy::ct::eq(std::span<const std::byte>{}, std::span<const std::byte>{two_bytes}));
}  // namespace

int main() { return 0; }
