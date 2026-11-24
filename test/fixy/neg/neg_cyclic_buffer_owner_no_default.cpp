// The maker cyclic_buffer_owner value-initializes each slot, and a fresh claim
// starts a new element in a slot after it ends the old one.  An element whose
// default constructor can throw would leave a slot with no element, so the
// maker refuses it, and the gate of the mint refuses the maker.

#include <fixy/CyclicBuffer.h>

namespace {

struct ThrowingDefault {
    ThrowingDefault() noexcept(false) {}
};

}  // namespace

int main() {
    auto minted = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<ThrowingDefault, 4>);
    static_cast<void>(minted);
    return 0;
}
