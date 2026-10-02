// A brace is a placeholder of the formatter of the Report family.  A text
// with a brace would change its meaning when the formatter gives the brace
// a value, so a text of a report holds no brace.

#include <foundation/core/Report.h>

int main() { ::foundation::core::fatal("the queue holds {} items"); }
