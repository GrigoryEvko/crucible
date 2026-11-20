// A brand names one borrow and carries no state.  A brand with a data member
// would give the Ref bytes that no door wrote.  The type is spellable although
// no door builds it, and the IsBrand constraint of the class template refuses
// the spelling.

#include <foundation/core/Ref.h>

namespace {
struct StatefulBrand {
    int count = 0;
};
}  // namespace

int main() { return static_cast<int>(sizeof(::foundation::core::Ref<int, StatefulBrand>)); }
