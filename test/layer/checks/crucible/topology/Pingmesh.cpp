// The compile-time checks of crucible/topology/Pingmesh.h.

#include <crucible/topology/Pingmesh.h>

namespace crucible::topology {

namespace detail {

static_assert(alignof(AtomicPingmeshPairCounters) >= 64,
              "AtomicPingmeshPairCounters must be cache-line-aligned so that "
              "adjacent slots in the Pingmesh counters_ grid land on distinct "
              "lines under concurrent per-pair recording");
static_assert(sizeof(AtomicPingmeshPairCounters) >= 64,
              "AtomicPingmeshPairCounters occupies a full cache line; trailing "
              "padding is intentional — see false-sharing rationale above");

}  // namespace detail

static_assert(!CtxFitsPingmeshMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsPingmeshMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPingmeshRecord<::fixy::HotFgCtx>);
static_assert(CtxFitsPingmeshRecord<::fixy::BgDrainCtx>);
static_assert(std::is_base_of_v<::foundation::Pinned<Pingmesh<2>>, Pingmesh<2>>);
static_assert(!std::is_constructible_v<Pingmesh<2>, PingmeshConfig>,
              "a pingmesh is reached only through mint_pingmesh");

}  // namespace crucible::topology
