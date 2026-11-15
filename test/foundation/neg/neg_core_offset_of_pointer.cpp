// offset_of takes two Views, which carry their counts.  Two pointers carry
// no count, so no test can tell that the part lies inside the whole, and
// offset_of refuses them.

#include <foundation/core/Region.h>

int main() {
    int whole = 1;
    int part = 2;
    return static_cast<int>(::foundation::core::offset_of(&whole, &part).is_none());
}
