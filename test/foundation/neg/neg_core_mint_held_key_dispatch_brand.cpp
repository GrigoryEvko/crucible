// A holder names a dispatch brand as its held brand, so mint_held_key would give
// a key of a dispatch brand outside the window of the dispatch door, and a body
// of a dispatch could then meet a second key of its brand.  HeldKeyBrand refuses
// a dispatch brand, so the mint does not take it.

#include <foundation/core/KeyedMint.h>

struct[[= foundation::core::dispatch_brand{}]] DispatchBrand final {
    class key_proof final {
        friend int main();

        constexpr key_proof() noexcept {}

    public:
        key_proof(key_proof const&) = delete("a proof of a held key stays with its holder");
    };
};

int main() {
    auto const& key = foundation::core::mint_held_key<DispatchBrand>(DispatchBrand::key_proof{});
    static_cast<void>(key);
    return 0;
}
