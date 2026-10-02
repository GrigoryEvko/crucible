// Each placeholder of a text of a report takes one argument.  A text with a
// placeholder and no argument for it would write nothing at that place, so
// it stops the build at the call.

#include <foundation/core/Report.h>

int main() { ::foundation::core::fatal("the queue holds {} items"); }
