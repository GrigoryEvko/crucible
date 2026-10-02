// A placeholder of a text of a report is `{}` and holds no specification.
// A text with `{:x}` asks for a form that the formatter does not give, so
// it stops the build at the call.

#include <foundation/core/Report.h>

int main() { ::foundation::core::fatal("the mask is {:x}", 3); }
