// foundation::diag::tag_base is the root of the diagnostic tags and not a
// tag of its own, so mint_diagnostic has no candidate for it.

#include <foundation/diag/Catalog.h>

int main() {
    auto diagnostic = ::foundation::diag::mint_diagnostic<::foundation::diag::tag_base>();
    (void)diagnostic;
    return 0;
}
