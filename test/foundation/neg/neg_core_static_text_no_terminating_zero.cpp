// A static text has a zero after its last character.  The mint reads up to
// the zero, so a character with no zero after it makes the mint read past
// the end of its object, which is no constant expression, and the build
// stops at the call.

#include <foundation/core/Text.h>

namespace {
struct Letter {
    char value = 'a';
};
constexpr Letter letter{};
constexpr ::foundation::core::TextView text = ::foundation::core::mint_static_text(&letter.value);
}  // namespace

int main() { return static_cast<int>(text.size()); }
