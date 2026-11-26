// A unit that is no holder of a held brand builds the proof of the brand, so it
// could mint a second key of the brand and open the owner of a holder with it.
// The constructor of the proof is private, and its one friend is the holder,
// so mint_held_key gets no proof here.

#include <foundation/core/KeyedMint.h>

class Holder;

struct HeldBrand final {
    class key_proof final {
        friend class Holder;

        constexpr key_proof() noexcept {}

    public:
        key_proof(key_proof const&) = delete("a proof of a held key stays with its holder");
    };
};

int main() {
    auto key = foundation::core::mint_held_key<HeldBrand>(HeldBrand::key_proof{});
    static_cast<void>(key);
    return 0;
}
