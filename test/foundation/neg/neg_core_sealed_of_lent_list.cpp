// A read of a GrowList writes its lend count, so two threads cannot read one
// list at the same time, and the list does not seal.

#include <foundation/core/Grow.h>
#include <foundation/core/Sealed.h>

int main() {
    // The size is the one use of the type.
    static_cast<void>(sizeof(foundation::core::Sealed<foundation::core::GrowList<int>>));
    return 0;
}
