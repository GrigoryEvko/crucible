#pragma once

// The brand and the entry points that two shared objects export for
// test_producer_claim_across_libraries.  producer_claim_owner.cpp builds and
// destroys the brand, and producer_claim_winner.cpp wins its claim, so the
// win and the destructor of one claim are in two shared objects.

#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>

namespace producer_claim_libraries {

struct SharedBrand {
    ::foundation::effects::host::ProducerClaim<SharedBrand> claim;
};

// Defined in the owner library.  The storage and the destructor of the claim
// are there.  destroy_brand takes the Box by reference, because under the
// Itanium C++ ABI the caller ends a parameter that a call takes by value.
[[gnu::visibility("default"), nodiscard]] ::foundation::core::Box<SharedBrand> make_brand() noexcept;
[[gnu::visibility("default")]] void destroy_brand(::foundation::core::Box<SharedBrand>&& brand) noexcept;

// Defined in the winner library.  The claim is won there, on the calling thread.
[[gnu::visibility("default")]] void win_claim(SharedBrand& brand);

}  // namespace producer_claim_libraries
