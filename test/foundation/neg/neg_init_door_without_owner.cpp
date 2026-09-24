// The door of the init context is a static member of the init owner.
// So each call contains the name of the owner, and the door guard finds
// each call through that name.  No free function opens the door, so a
// call without the owner finds no door.

#include <foundation/effects/Effect.h>

int main() {
    auto init = ::foundation::effects::mint_init_context();
    (void)init;
    return 0;
}
