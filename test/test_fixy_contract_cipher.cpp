// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, and pins each re-export to the
// substrate entity it must name.

#include <crucible/fixy/Contract.h>

#include <type_traits>

namespace fc = ::crucible::fixy::contract;
namespace fcc = ::crucible::fixy::contract::cipher;
namespace cs = ::crucible::safety;
namespace cc = ::crucible::cipher;
namespace cproto = ::crucible::safety::proto;

static_assert(
    std::is_same_v<fcc::CipherTier<cs::CipherTierTag_v::Hot, int>, cs::CipherTier<cs::CipherTierTag_v::Hot, int>>,
    "fixy::contract::cipher::CipherTier must alias safety::CipherTier.");

static_assert(std::is_same_v<fcc::HotTierHandle<int>, cs::cipher_tier::Hot<int>>,
              "fixy::contract::cipher::HotTierHandle must alias cipher_tier::Hot.");

static_assert(std::is_same_v<fcc::WarmTierHandle<double>, cs::cipher_tier::Warm<double>>,
              "fixy::contract::cipher::WarmTierHandle must alias cipher_tier::Warm.");

static_assert(std::is_same_v<fcc::ColdTierHandle<int>, cs::cipher_tier::Cold<int>>,
              "fixy::contract::cipher::ColdTierHandle must alias cipher_tier::Cold.");

// The using-declaration brings a whole overload set across, so the cast to
// a concrete function-pointer type is what selects one instantiation and
// makes the comparison well formed.

static_assert(
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
        &fcc::mint_promote<cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot, int>)
        == static_cast<
            cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
            &cc::mint_promote<cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot, int>),
    "fixy::contract::cipher::mint_promote must be the substrate mint.");

static_assert(
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Cold, int> (*)(cs::CipherTier<cs::CipherTierTag_v::Hot, int>)>(
        &fcc::mint_demote<cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold, int>)
        == static_cast<
            cs::CipherTier<cs::CipherTierTag_v::Cold, int> (*)(cs::CipherTier<cs::CipherTierTag_v::Hot, int>)>(
            &cc::mint_demote<cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold, int>),
    "fixy::contract::cipher::mint_demote must be the substrate mint.");

static_assert(fcc::can_promote_tier_v<cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot>
              == cc::can_promote_tier_v<cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot>);

static_assert(!fcc::can_promote_tier_v<cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold>);

static_assert(fcc::can_demote_tier_v<cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold>
              == cc::can_demote_tier_v<cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold>);

using SendInt = cproto::Send<int, cproto::End>;

static_assert(std::is_same_v<fcc::EpochedDelegate<SendInt, cproto::End, 0, 0>,
                             cproto::EpochedDelegate<SendInt, cproto::End, 0, 0>>,
              "fixy::contract::cipher::EpochedDelegate must alias "
              "safety::proto::EpochedDelegate.");

static_assert(std::is_same_v<fcc::EpochedDelegate<SendInt, cproto::End, 5, 3>,
                             cproto::EpochedDelegate<SendInt, cproto::End, 5, 3>>);

int main() { return 0; }
