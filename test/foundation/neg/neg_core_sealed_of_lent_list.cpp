// A read of a SmallList writes its lend count, so two threads cannot read one
// list at the same time, and the list does not seal.

#include <foundation/core/Sealed.h>
#include <foundation/core/SmallList.h>

int main() {
    // The size is the one use of the type.
    static_cast<void>(sizeof(foundation::core::Sealed<foundation::core::SmallList<int, 2>>));
    return 0;
}
