// The empty-slot door builds a Tagged holding T{}.  For a tag that names
// where a value came from, that is an empty slot.  For an earned tag it is
// a claim that a check passed on a value no check ever saw, so the default
// constructor is closed for every earned tag.

#include <fixy/Tagged.h>

namespace tags = ::fixy::tags;

int main() {
    fixy::Tagged<int, tags::trust::Verified> forged{};
    return forged.value();
}
