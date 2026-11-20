// for_each_enumerator_value hands each enumerator to the callback as a
// value.  A callback that takes no argument has no call that matches, so
// the walk refuses it where it calls the callback.
#include <foundation/reflect/EnumName.h>

namespace {
enum class Signal : unsigned char {
    Alpha = 0x01,
};
}  // namespace

int main() {
    ::foundation::reflect::for_each_enumerator_value<Signal>([] {});
    return 0;
}
