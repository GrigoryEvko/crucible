// address_of takes a Ref, which is never null.  A pointer can be null, so
// address_of refuses it.

#include <foundation/core/Ref.h>

int main() {
    int number = 1;
    static_cast<void>(::foundation::core::address_of(&number));
    return 0;
}
