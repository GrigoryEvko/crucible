// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MPMC endpoint fixture 1 for
// safety::proto::mpmc_channel_session::mint_mpmc_producer_endpoint
// (token mint, single-argument, sessions/MpmcChannelSession.h).  The
// template-parameter constraint `MpmcChannelSessionSurface Channel` rejects
// a plain type that exposes NONE of the required surface — it fails at the
// very first requirement (`typename Channel::value_type`).
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_producer_endpoint_near_miss.cpp (#2): there a type
// satisfies EVERY surface requirement except producer()'s return-type
// contract; here the type is not a channel at all and fails on the first
// nested-type requirement.
//
// Expected diagnostic: MpmcChannelSessionSurface / constraints not
// satisfied / no matching function.

#include <crucible/sessions/MpmcChannelSession.h>

namespace fmpmc = ::crucible::safety::proto::mpmc_channel_session;

namespace neg_fixy_substr_mpmc_producer_endpoint_non_surface {
// No nested types, no producer()/consumer() — MpmcChannelSessionSurface
// must reject at the first `typename Channel::value_type` requirement.
struct FakeChannel {};
}  // namespace neg_fixy_substr_mpmc_producer_endpoint_non_surface

int main() {
    neg_fixy_substr_mpmc_producer_endpoint_non_surface::FakeChannel fake{};

    [[maybe_unused]] auto bad = fmpmc::mint_mpmc_producer_endpoint(fake);
    return 0;
}
