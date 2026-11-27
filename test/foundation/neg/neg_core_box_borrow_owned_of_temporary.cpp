// The owner door of a Box gives a borrow of its object, and a temporary Box
// ends the object at the end of the full expression.  The deleted twin of the
// door refuses a temporary Box.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

struct Holder {
    [[nodiscard]] static int read() noexcept {
        return ::foundation::core::borrow_owned(
            ::foundation::core::box_owner_key<Holder>{},
            ::foundation::core::mint_box<int>(::foundation::effects::testing::test().alloc, 1));
    }
};

int main() { return Holder::read(); }
