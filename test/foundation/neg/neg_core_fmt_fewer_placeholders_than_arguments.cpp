// Each argument of a report has one placeholder in the text.  An argument
// with no placeholder would not be written, so it stops the build at the
// call.

#include <foundation/core/Report.h>

int main() { ::foundation::core::fatal("the queue is full", 3); }
