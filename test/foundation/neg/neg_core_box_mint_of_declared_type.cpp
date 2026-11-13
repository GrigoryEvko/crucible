// A Box makes its object only where the type of the object is complete.
// The mint of a Box of a type that is only declared is refused.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

struct Engine;

int main() {
    static_cast<void>(::foundation::core::mint_box<Engine>(::foundation::effects::Alloc{}));
    return 0;
}
