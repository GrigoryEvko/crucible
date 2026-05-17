// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture: mint_restore via fixy::contract::cipher
// rejects when the payload T is not move-constructible.
//
// Violation: mint_restore<T> requires RestorableTier<T>, which
// reduces to std::move_constructible<T>.  An immovable T fails the
// requires-clause.  Routing through the fixy:: alias must reject
// identically to the substrate fixture
// neg_cipher_tier_restore_unmovable_payload.
//
// Expected diagnostic: RestorableTier / move_constructible /
// constraints not satisfied.

#include <crucible/fixy/Contract.h>

#include <utility>

namespace fcipher = ::crucible::fixy::contract::cipher;

struct ImmovablePayload {
    ImmovablePayload() = default;
    ImmovablePayload(const ImmovablePayload&) = delete;
    ImmovablePayload& operator=(const ImmovablePayload&) = delete;
    ImmovablePayload(ImmovablePayload&&) = delete;
    ImmovablePayload& operator=(ImmovablePayload&&) = delete;
};

using BadMint = decltype(fcipher::mint_restore<ImmovablePayload>(
    std::declval<fcipher::ColdTierHandle<ImmovablePayload>>(),
    std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
