// A payload with a mutable member is refused.  peek() returns a const
// reference, a mutable member is writable through one, and the payload
// would then change under its version with no cast.

#include <fixy/EpochVersioned.h>

struct CachedValue {
    int value = 0;
    mutable int cache = 0;
};

int main() {
    fixy::EpochVersioned<CachedValue> const versioned{CachedValue{}, fixy::Epoch{2}, fixy::Generation{2}};
    return versioned.peek().value;
}
