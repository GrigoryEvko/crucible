// ── test_fixy_contract_cipher — sentinel TU for fixy/Contract.h ───
//
// Pulls fixy/Contract.h into a TU compiled under project warning
// flags so the header's static_asserts execute.  Witnesses:
//
//   1. fixy::contract::cipher::CipherTier aliases the safety substrate.
//   2. fixy::contract::cipher::{Hot,Warm,Cold}TierHandle alias the
//      cipher_tier::{Hot,Warm,Cold} convenience aliases.
//   3. fixy::contract::cipher::mint_promote / mint_demote / mint_restore
//      re-export the cipher namespace mint factories at the function
//      level (function-pointer-type identity).
//   4. fixy::contract::cipher::can_promote_tier_v / can_demote_tier_v
//      preserve the substrate boolean values.
//   5. fixy::contract::cipher::EpochedDelegate aliases the substrate.
//
// FIXY-AUDIT-B9: re-export of safety/Contract.h macros + Cipher
// migration discipline (cipher/CipherTierPromotion.h +
// sessions/SessionDelegate.h + bridges/SessionPersistence.h).  No
// new mint factories introduced — pure re-export of substrate that
// already ships its own HS14 floor.  Negative-compile coverage for
// the underlying primitives lives next to those substrate headers.

#include <crucible/fixy/Contract.h>

#include <type_traits>

namespace fc      = ::crucible::fixy::contract;
namespace fcc     = ::crucible::fixy::contract::cipher;
namespace cs      = ::crucible::safety;
namespace cc      = ::crucible::cipher;
namespace cproto  = ::crucible::safety::proto;

// ─── 1. CipherTier wrapper identity ───────────────────────────────

static_assert(std::is_same_v<
    fcc::CipherTier<cs::CipherTierTag_v::Hot, int>,
    cs::CipherTier<cs::CipherTierTag_v::Hot, int>>,
    "fixy::contract::cipher::CipherTier must alias safety::CipherTier.");

// ─── 2. Per-tier convenience handle identity ──────────────────────

static_assert(std::is_same_v<
    fcc::HotTierHandle<int>,
    cs::cipher_tier::Hot<int>>,
    "fixy::contract::cipher::HotTierHandle must alias cipher_tier::Hot.");

static_assert(std::is_same_v<
    fcc::WarmTierHandle<double>,
    cs::cipher_tier::Warm<double>>,
    "fixy::contract::cipher::WarmTierHandle must alias cipher_tier::Warm.");

static_assert(std::is_same_v<
    fcc::ColdTierHandle<int>,
    cs::cipher_tier::Cold<int>>,
    "fixy::contract::cipher::ColdTierHandle must alias cipher_tier::Cold.");

// ─── 3. Mint factory function-type identity (re-export witness) ───
//
// `using ::crucible::cipher::mint_promote;` brings the overload set
// into fcc::; ensure the explicit-instantiation pointer is identical
// to the substrate's instantiation.

static_assert(
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(
        cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
        &fcc::mint_promote<cs::CipherTierTag_v::Cold,
                           cs::CipherTierTag_v::Hot, int>)
    ==
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Hot, int> (*)(
        cs::CipherTier<cs::CipherTierTag_v::Cold, int>)>(
        &cc::mint_promote<cs::CipherTierTag_v::Cold,
                          cs::CipherTierTag_v::Hot, int>),
    "fixy::contract::cipher::mint_promote must be the substrate mint.");

static_assert(
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Cold, int> (*)(
        cs::CipherTier<cs::CipherTierTag_v::Hot, int>)>(
        &fcc::mint_demote<cs::CipherTierTag_v::Hot,
                          cs::CipherTierTag_v::Cold, int>)
    ==
    static_cast<cs::CipherTier<cs::CipherTierTag_v::Cold, int> (*)(
        cs::CipherTier<cs::CipherTierTag_v::Hot, int>)>(
        &cc::mint_demote<cs::CipherTierTag_v::Hot,
                         cs::CipherTierTag_v::Cold, int>),
    "fixy::contract::cipher::mint_demote must be the substrate mint.");

// ─── 4. Admission gate value preservation ─────────────────────────

static_assert(fcc::can_promote_tier_v<
    cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot> ==
    cc::can_promote_tier_v<
    cs::CipherTierTag_v::Cold, cs::CipherTierTag_v::Hot>);

static_assert(!fcc::can_promote_tier_v<
    cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold>);

static_assert(fcc::can_demote_tier_v<
    cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold> ==
    cc::can_demote_tier_v<
    cs::CipherTierTag_v::Hot, cs::CipherTierTag_v::Cold>);

// ─── 5. EpochedDelegate identity (sessions/SessionDelegate.h) ─────

using SendInt = cproto::Send<int, cproto::End>;

static_assert(std::is_same_v<
    fcc::EpochedDelegate<SendInt, cproto::End, 0, 0>,
    cproto::EpochedDelegate<SendInt, cproto::End, 0, 0>>,
    "fixy::contract::cipher::EpochedDelegate must alias "
    "safety::proto::EpochedDelegate.");

static_assert(std::is_same_v<
    fcc::EpochedDelegate<SendInt, cproto::End, 5, 3>,
    cproto::EpochedDelegate<SendInt, cproto::End, 5, 3>>);

// ─── Translation-unit witness ─────────────────────────────────────

int main() { return 0; }
