// A pointer payload is refused.  The referent can change after the value
// is built, and the version would then describe a value it never saw.

#include <fixy/EpochVersioned.h>

int main() {
    int target = 1;
    auto const handle = fixy::EpochVersioned<int*>::at_genesis(&target);
    return *handle.peek();
}
