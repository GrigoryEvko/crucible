// A borrow of a temporary CacheLine dangles at the end of the full
// expression.  The rvalue overload of get() is deleted.

#include <foundation/core/Atomic.h>

int main() {
    int& dangling = ::foundation::core::CacheLine<int>{3}.get();
    return dangling;
}
