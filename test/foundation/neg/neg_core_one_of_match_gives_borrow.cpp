// The result of an rvalue match holds no address.  The match cannot tell an
// address of the alternative from an address of a different object, so it
// refuses each address, also the address of a string literal, which outlives
// the match.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    closed
};

int main() {
    ::foundation::core::TextView const chosen = ::foundation::core::OneOf<int, Code>{7}.match(
        [](int) noexcept { return ::foundation::core::TextView{"first"}; },
        [](Code) noexcept { return ::foundation::core::TextView{"second"}; });
    return chosen.size() == 5 ? 0 : 1;
}
