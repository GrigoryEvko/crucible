// mint_c_text gives the one CText of a view that holds its zero, so a call
// that drops its result is refused.

#include <foundation/core/Text.h>

int main() {
    ::foundation::core::mint_c_text(::foundation::core::TextView{"ok"});
    return 0;
}
