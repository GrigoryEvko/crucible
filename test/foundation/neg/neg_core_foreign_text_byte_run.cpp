// mint_foreign_text takes a run of char.  A run of bytes is no text, and
// mint_foreign_view gives its View.

#include <foundation/core/Region.h>

int main() {
    auto const text = ::foundation::core::mint_foreign_text(static_cast<unsigned char const*>(nullptr), 0);
    return static_cast<int>(text.size());
}
