// S011: a capability x a replay-deterministic payload, at the Pure tier.
//
// A capability is an authorization token minted for one run, and replay
// cannot mint the same token again.  A payload whose DetSafe band claims
// the same bits on every replay therefore cannot hold one.
//
// The trust grade is stated so that T001, which also reads
// capability_usage, stands down: a capability with no trust grade is
// unverified.  With it, the pack trips S011 alone.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>, ::fixy::atom::capability_usage,
                                ::fixy::atom::trust_verified>
        refused{};
    return 0;
}
