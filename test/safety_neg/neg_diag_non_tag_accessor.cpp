// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `diagnostic_name_v<int>` where T is NOT a
// diagnostic tag.  The `accessor_check<T, false>` specialization
// fires its routed `static_assert` with the framework-controlled
// `[DiagnosticAccessor_NonTag]` diagnostic, pointing at
// `safety::diag::tag_base`'s catalog.

#include <crucible/safety/Diagnostic.h>

// Force instantiation.
constexpr auto bogus_name = ::crucible::safety::diag::diagnostic_name_v<int>;
auto const* g_addr = &bogus_name;

int main() { return 0; }
