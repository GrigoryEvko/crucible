// exchange moves the old value out of the slot: a Pinned type does not move,
// so the exchange of one does not compile.

#include <foundation/Pinned.h>
#include <foundation/core/Values.h>

struct Anchored : ::foundation::Pinned<Anchored> {
    int value = 0;
};

int main() {
    Anchored slot{};
    return ::foundation::core::exchange(slot, Anchored{}).value;
}
