// mint_address_key takes a pointer to an object or a void pointer.  A
// pointer to a function names code and not data, so the mint refuses it.
// The parameter is a pointer to a const T, and a function type takes no
// const, so the deduction fails before the concept AddressKeyTarget.

#include <foundation/core/Ref.h>

namespace {

int answer() noexcept { return 42; }

}  // namespace

int main() {
    static_cast<void>(::foundation::core::mint_address_key(&answer));
    return 0;
}
