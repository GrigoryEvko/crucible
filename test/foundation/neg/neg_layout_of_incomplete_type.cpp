// Layout::of takes a complete object type.  A type that is only declared has
// no size, so its layout does not compile.

#include <foundation/alloc/Layout.h>

struct Opaque;

int main() {
    auto const layout = ::foundation::alloc::Layout::of<Opaque>();
    return static_cast<int>(layout.bytes().raw());
}
