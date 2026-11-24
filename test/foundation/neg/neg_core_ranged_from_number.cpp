// No number becomes a Ranged with no check: the constructor from a word is
// private, and check() is the door of a number.

#include <foundation/core/Index.h>

#include <cstdint>

int main() {
    ::foundation::core::Ranged<0, 7> const value{std::uint8_t{9}};
    return static_cast<int>(value.raw());
}
