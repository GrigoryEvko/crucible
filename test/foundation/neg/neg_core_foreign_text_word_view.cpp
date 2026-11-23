// mint_foreign_text takes the bytes of a View, one character for each byte.
// A View of wider elements is no run of characters, so no form of the mint
// takes it, and a reader of 16-bit words gets no text of half words.

#include <foundation/core/Region.h>

#include <cstdint>

int main() {
    auto const text = ::foundation::core::mint_foreign_text(::foundation::core::View<std::uint16_t const>{});
    return static_cast<int>(text.characters().size());
}
