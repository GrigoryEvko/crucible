// A key that a caller builds from an empty braced list would read the empty
// value of the niche of a Ref.  Only the reader of the niche builds a key,
// so the constructor of the key is private.

#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::Ref<int> const forged = ::foundation::core::niche<::foundation::core::Ref<int>>::empty({});
    return *forged;
}
