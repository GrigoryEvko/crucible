// Negative-compile test.  This file must fail to compile.
//
// An ExternalSlotId names an external slot of the plan of the active region.
// The list of the external reads of a context holds each one, and each change
// of the active region drops the list.  The class is private to the context,
// so no code keeps an id of one activation and gives it to the next one.

#include <crucible/CrucibleContext.h>

int main() {
    ::fixy::Option<crucible::CrucibleContext::ExternalSlotId> kept{::fixy::none};
    return kept.is_some() ? 1 : 0;
}
