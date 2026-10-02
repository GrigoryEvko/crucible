// A Box owns its object alone.  A copy would free the object two times.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::Box<int> first = ::foundation::core::mint_box<int>(::foundation::effects::Alloc{}, 1);
    ::foundation::core::Box<int> second = first;
    return second.get();
}
