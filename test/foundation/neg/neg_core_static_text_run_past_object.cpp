// mint_static_text(first, size) reads the last character of the run, so a
// count past the end of the object of static storage stops the build.

#include <foundation/core/Text.h>

int main() {
    constexpr foundation::core::TextView text = foundation::core::mint_static_text("abc", 8);
    return static_cast<int>(text.size());
}
