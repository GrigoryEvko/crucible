// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 1 #4 (#857): IsOneToOneSubstrate selective-dispatch witness.
//
// Violation: a function constrained on `IsOneToOneSubstrate<S>`
// only accepts SPSC substrates.  An MPMC substrate is offered;
// the constraint fails.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at IsOneToOneSubstrate.

#include <crucible/concurrent/Substrate.h>

namespace conc = crucible::concurrent;

struct ProducerStream {};

template <conc::IsOneToOneSubstrate S>
constexpr void requires_spsc(S const&) noexcept {}

int main() {
    using MpmcT = conc::Substrate_t<conc::ChannelTopology::ManyToMany,
                                     int, 64, ProducerStream>;
    MpmcT* mpmc_ptr = nullptr;  // type-only test; no instantiation needed
    (void)mpmc_ptr;
    requires_spsc(*mpmc_ptr);   // overload resolution fails before any deref
    return 0;
}
