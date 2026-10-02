// consume() gives up the Box.  A call on an lvalue would leave a named Box
// with no object in scope, so the caller must write std::move and the
// use-after-move guard then sees each later use.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::Box<int> held = ::foundation::core::mint_box<int>(::foundation::effects::Alloc{}, 3);
    return held.consume();
}
