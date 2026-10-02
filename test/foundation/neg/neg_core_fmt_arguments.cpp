// A text of a report takes no argument until the formatter of the Report
// family checks each argument against its placeholder.

#include <foundation/core/Report.h>

int main() { return sizeof(::foundation::core::Fmt<int>) > 0 ? 0 : 1; }
