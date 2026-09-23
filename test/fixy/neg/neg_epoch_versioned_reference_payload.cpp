// A reference payload is refused.  The referent can change after the
// value is built, and the version would then describe a value it never
// saw.

#include <fixy/EpochVersioned.h>

int main() {
    int target = 1;
    fixy::EpochVersioned<int&> const aliased{target, fixy::Epoch{2}, fixy::Generation{2}};
    return aliased.peek();
}
