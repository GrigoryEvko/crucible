// A brace in a text of a report is a placeholder `{}` or an escape `{{` or
// `}}`.  A lone brace is neither, so it stops the build at the call.

#include <foundation/core/Report.h>

int main() { ::foundation::core::report(::foundation::core::Sink::Err, "the set { a, b\n"); }
