// Copying a Linear<T> would create two owners of one resource.  The
// copy constructor is deleted with a reason, and the compiler names the
// deleted Qtt copy constructor and repeats that reason.

#include <fixy/Qtt.h>

int main() {
    fixy::Linear<int> owned = fixy::mint_linear<int>(42);
    fixy::Linear<int> duplicate = owned;
    return duplicate.peek();
}
