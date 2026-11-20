// mint_static_text is consteval, so its text is a constant of the compile.
// The address of a character that the program holds on its stack is no
// constant expression, so the view of its characters is refused at the call.

#include <foundation/core/Text.h>

int main() {
    char letter = 'a';
    return static_cast<int>(::foundation::core::mint_static_text(&letter).size());
}
