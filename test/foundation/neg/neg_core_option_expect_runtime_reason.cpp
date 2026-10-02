// The reason of expect() is a string literal, so a reader finds its end.  A
// pointer carries no length and no proof of an end, so it does not convert
// to the reason, also when it points at a literal.

#include <foundation/core/Choice.h>

int main() {
    return ::foundation::core::Option<int>::some(1).expect(static_cast<char const*>("the option holds a value"));
}
