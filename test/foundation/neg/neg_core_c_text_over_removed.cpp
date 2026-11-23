// The free door detail::c_text_over_ built a CText of any pointer and any
// count, with no zero after it, for each unit of the base.  The door is gone:
// a CText comes from a literal, from mint_c_text, which checks the zero, or
// from the door of an owner that keeps the zero.

#include <foundation/core/Text.h>

int main() {
    foundation::core::CText const text = foundation::core::detail::c_text_over_(static_cast<char const*>(nullptr), 4);
    return static_cast<int>(text.size());
}
