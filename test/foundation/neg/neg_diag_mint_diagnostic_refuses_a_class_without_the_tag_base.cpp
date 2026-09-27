// A class that does not derive from foundation::diag::tag_base is not a
// diagnostic tag, so mint_diagnostic has no candidate for it.  The gate
// asks for the lineage, not only for a class type.

#include <foundation/diag/Catalog.h>

struct LooksLikeADiagnosticTag {};

int main() {
    auto diagnostic = ::foundation::diag::mint_diagnostic<LooksLikeADiagnosticTag>();
    (void)diagnostic;
    return 0;
}
