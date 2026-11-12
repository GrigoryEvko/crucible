// A OneOf compares only with a OneOf of its own alternatives.  The
// implicit constructor from a value makes no compare: the active
// alternative has its own test, is<T>().

#include <foundation/core/OneOf.h>

int main() {
    ::foundation::core::OneOf<int, long> const either{7};
    return either == 7 ? 0 : 1;
}
