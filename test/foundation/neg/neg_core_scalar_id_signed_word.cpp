// The word of an Id is unsigned, so its highest value is the niche: an Id of
// an int does not compile.

#include <foundation/core/Id.h>

int main() { return ::foundation::core::Id<struct SlotTag, int>{3}.raw() == 3 ? 0 : 1; }
