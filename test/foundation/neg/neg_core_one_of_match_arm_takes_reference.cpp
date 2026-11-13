// The rvalue match gives up the OneOf, which is a temporary here, so an arm
// that takes its alternative by reference names an object that the full
// expression ends.  A generic arm with a forwarding parameter takes a
// reference too, so the rvalue match refuses it.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    closed
};

int main() {
    return ::foundation::core::OneOf<int, Code>{7}.match([](auto&& value) noexcept { return static_cast<int>(value); },
                                                         [](Code) noexcept { return 0; });
}
