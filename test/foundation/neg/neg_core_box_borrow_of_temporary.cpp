// A borrow of a temporary Box dangles at the end of the full expression,
// because the destructor of the Box frees the object.  The rvalue overload
// of get() is deleted.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() {
    int& dangling = ::foundation::core::mint_box<int>(::foundation::effects::Alloc{}, 1).get();
    return dangling;
}
