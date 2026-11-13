// mint_c_text takes a view that holds its zero.  A bare address gives no
// count, so a reader cannot know where the text ends, and the mint refuses it.

#include <foundation/core/Text.h>

int main() {
    auto const text = ::foundation::core::mint_c_text(static_cast<char const*>(nullptr));
    return text.is_some() ? 0 : 1;
}
