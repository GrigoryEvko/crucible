// The checked lifetime start refuses a refined value.  A refined value
// keeps a trivial copy constructor, so it is an implicit-lifetime type, and
// the class carries the annotation no_start_over_bytes.  start_as_array
// refuses it at its constraint.  A value started over bytes would hold a
// number that its predicate never checked.

#include <fixy/Refined.h>
#include <foundation/Lifetime.h>

int main() {
    using Positive = fixy::Refined<fixy::positive, int>;
    alignas(Positive) unsigned char bytes[sizeof(Positive)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Positive>(bytes, 1);
    return static_cast<int>(forged.size());
}
