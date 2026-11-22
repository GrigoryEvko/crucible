// mint_cyclic_buffer gives the fresh brand of its call site, and no caller
// names it.  A caller that names a brand of its own could mint a second key
// of the brand of a ring that exists, so the mint refuses each explicit
// argument after the element and the count.

#include <fixy/CyclicBuffer.h>

#include <cstdint>

namespace {

struct ChosenBrand final {};

}  // namespace

int main() {
    auto minted = ::fixy::mint_cyclic_buffer<std::uint64_t, 4, ChosenBrand>();
    static_cast<void>(minted);
    return 0;
}
