// The text of fatal() is a string literal, so a reader finds its end.  A
// pointer carries no length and no proof of an end, so it does not convert
// to a text of a report, also when it points at a literal.

#include <foundation/core/Report.h>

int main() { ::foundation::core::fatal(static_cast<char const*>("the queue is full")); }
