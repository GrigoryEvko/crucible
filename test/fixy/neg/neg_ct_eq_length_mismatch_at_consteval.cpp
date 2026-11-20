// ct::eq carries CRUCIBLE_PRE(a.size() == b.size()).  An empty View and
// a View of two bytes reach the macro's consteval trap, so the static_assert
// below has a non-constant condition.  Without the precondition the loop
// runs over no bytes and eq answers true, so the fixture compiles.
#include <fixy/ConstantTime.h>
#include <fixy/Core.h>

#include <cstddef>

namespace {
constexpr ::fixy::FixedArray<std::byte, 2> two_bytes{std::byte{0x42}, std::byte{0x43}};
static_assert(::fixy::ct::eq(::fixy::View<std::byte const>{}, ::fixy::View<std::byte const>{two_bytes.view()}));
}  // namespace

int main() { return 0; }
