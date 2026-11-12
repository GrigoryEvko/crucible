// A CacheLine gives its cell through the dereference.  The verb get is the
// checked lookup with a key, so a CacheLine has no member get.

#include <foundation/core/Atomic.h>

int main() {
    ::foundation::core::CacheLine<int> line{3};
    return line.get();
}
