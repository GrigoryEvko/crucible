// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-045-audit fixture #3 for fixy::substr::spsc::mint_spsc_consumer_endpoint
// (token mint, two-argument, fixy/Substr.h v045::).  The template-parameter
// constraint `SpscChannelSessionSurface Channel` rejects a plain type that
// exposes NONE of the required surface — it fails at the very first
// requirement (`typename Channel::value_type`).
//
// Distinct mismatch class from
// neg_fixy_substr_spsc_consumer_endpoint_near_miss.cpp (#4): there a type
// satisfies EVERY surface requirement except consumer()'s return-type
// contract; here the type is not a channel at all and fails on the first
// nested-type requirement.
//
// Substitution-failure path: the function's second parameter is
// `Permission<typename Channel::consumer_tag>&&`.  With Channel=FakeChannel,
// `typename FakeChannel::consumer_tag` is invalid → parameter-list
// substitution fails → the template is silently removed from the overload
// set → "no matching function" with the surface concept also flagged.
//
// Expected diagnostic: SpscChannelSessionSurface / constraints not
// satisfied / no matching function / consumer_tag.

#include <utility>

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fsubstr = ::crucible::fixy::substr;
namespace saf     = ::crucible::safety;

namespace neg_fixy_substr_spsc_consumer_endpoint_non_surface {
// No nested types, no producer()/consumer() — SpscChannelSessionSurface
// must reject at the first `typename Channel::value_type` requirement,
// and the function-template parameter list cannot even be substituted
// because `typename FakeChannel::consumer_tag` does not exist.
struct FakeChannel {};

// Placeholder permission tag — used only to construct *something* to
// pass as the second argument; its type does not match the (non-existent)
// FakeChannel::consumer_tag, but we never get that far — substitution
// fails earlier.
struct consumer_tag_placeholder {};
}  // namespace neg_fixy_substr_spsc_consumer_endpoint_non_surface

int main() {
    neg_fixy_substr_spsc_consumer_endpoint_non_surface::FakeChannel fake{};
    auto perm = saf::mint_permission_root<
        neg_fixy_substr_spsc_consumer_endpoint_non_surface::consumer_tag_placeholder>();

    [[maybe_unused]] auto bad =
        fsubstr::spsc::mint_spsc_consumer_endpoint(fake, std::move(perm));
    return 0;
}
