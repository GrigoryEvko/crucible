// The empty value of the niche of a Ref holds the null pointer, which no Ref
// holds.  A call of the niche protocol with no key would give a null Ref to
// any caller, so each member of the protocol takes the key.

#include <foundation/core/Ref.h>

struct ForgeBrand {};

int main() {
    ::foundation::core::Ref<int, ForgeBrand> const forged =
        ::foundation::core::niche<::foundation::core::Ref<int, ForgeBrand>>::empty();
    return *forged;
}
