// mint_cyclic_buffer value-initializes each slot, and a fresh claim starts a
// new element in a slot after it ends the old one.  An element whose default
// constructor can throw would leave a slot with no element, so the mint
// refuses it.

#include <fixy/CyclicBuffer.h>

namespace {

struct ThrowingDefault {
    ThrowingDefault() noexcept(false) {}
};

}  // namespace

int main() {
    auto minted = ::fixy::mint_cyclic_buffer<ThrowingDefault, 4>();
    static_cast<void>(minted);
    return 0;
}
