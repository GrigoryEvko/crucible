// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074k fixture #3 for fixy::substr::mpmc::mint_mpmc_consumer_endpoint
// (token mint, single-argument, MpmcChannelSession.h:246).  The
// template-parameter constraint `MpmcChannelSessionSurface Channel` rejects
// a plain type that exposes NONE of the required surface — it fails at the
// very first requirement (`typename Channel::value_type`).
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_consumer_endpoint_near_miss.cpp (#4): there a type
// satisfies EVERY surface requirement except consumer()'s return-type
// contract; here the type is not a channel at all and fails on the first
// nested-type requirement.
//
// Expected diagnostic: MpmcChannelSessionSurface / constraints not
// satisfied / no matching function.

#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;

namespace neg_fixy_substr_mpmc_consumer_endpoint_non_surface {
// No nested types, no producer()/consumer() — MpmcChannelSessionSurface
// must reject at the first `typename Channel::value_type` requirement.
struct FakeChannel {};
}  // namespace neg_fixy_substr_mpmc_consumer_endpoint_non_surface

int main() {
    neg_fixy_substr_mpmc_consumer_endpoint_non_surface::FakeChannel fake{};

    [[maybe_unused]] auto bad =
        fsubstr::mpmc::mint_mpmc_consumer_endpoint(fake);
    return 0;
}
