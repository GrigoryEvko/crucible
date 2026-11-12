// The tag of an Id is a class that names its kind: an Id with the tag int
// does not compile.

#include <foundation/core/Id.h>

#include <cstdint>

int main() { return ::foundation::core::Id<int, std::uint32_t>{3}.raw() == 3u ? 0 : 1; }
