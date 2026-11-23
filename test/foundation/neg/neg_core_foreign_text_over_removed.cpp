// The free door detail::foreign_text_over_ made a ForeignText of any TextView
// for each unit of the base, so a text with no foreign source could claim
// one.  The door is gone: only mint_foreign_text, which checks the parts of a
// foreign run, makes a ForeignText.

#include <foundation/core/Text.h>

int main() {
    foundation::core::ForeignText const text =
        foundation::core::detail::foreign_text_over_(foundation::core::TextView{});
    return static_cast<int>(text.characters().size());
}
