// get<T>() of a temporary OneOf would give a Ref to an alternative that the
// full expression ends, so the rvalue form is deleted.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    closed
};

int main() { return ::foundation::core::OneOf<int, Code>{7}.get<int>().is_some() ? 0 : 1; }
