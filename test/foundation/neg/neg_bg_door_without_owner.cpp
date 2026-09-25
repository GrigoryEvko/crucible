// The door of the background context is a static member of the
// background owner.  So each call contains the name of the owner, and the
// door guard finds each call through that name.  No free function opens
// the door, so a call without the owner finds no door.

#include <foundation/effects/Effect.h>

int main() {
    auto bg = ::foundation::effects::mint_background_context();
    (void)bg;
    return 0;
}
