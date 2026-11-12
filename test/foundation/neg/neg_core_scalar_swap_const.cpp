// swap writes the two objects: a swap of two const objects does not compile.

#include <foundation/core/Values.h>

int main() {
    int const first = 1;
    int const second = 2;
    ::foundation::core::swap(first, second);
    return first;
}
