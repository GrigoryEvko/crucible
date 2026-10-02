// A floating value has no formatter in the Report family: its decimal form
// needs a rounding rule that the family does not give.  So a report refuses
// a floating argument at the call.

#include <foundation/core/Report.h>

int main() { ::foundation::core::fatal("the ratio is {}", 0.5); }
