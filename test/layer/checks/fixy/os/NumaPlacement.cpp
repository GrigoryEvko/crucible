// The compile-time checks of fixy/os/NumaPlacement.h.

#include <fixy/os/NumaPlacement.h>

namespace fixy::detail {

static_assert(numa_mask_bits % numa_mask_word_bits == 0);
static_assert(std::to_underlying(NumaNodeId::None) <= numa_mask_bits,
              "every concrete node must have a bit in the mask that mbind reads");

}  // namespace fixy::detail

namespace fixy::detail::numa_placement_invariants {

using numa_placement_witness::ProbeRegion;
using Placement = NumaPlacement<ProbeRegion, mmap::prot::WriteCopy>;
using Region = Placement::region_type;

// The gate, from a scope that the mint does not befriend.  Each cell
// names a route that builds a proof with no call to mbind.  test/fixy/neg/
// holds the same routes as negative-compile fixtures.
static_assert(!std::is_default_constructible_v<Placement>,
              "the default constructor of NumaPlacement must not be public: it claims a binding nobody made.");
static_assert(!std::is_constructible_v<Placement, Region, NumaNodeId>,
              "the value constructor of NumaPlacement must not be public: it claims a binding nobody made.");
static_assert(!std::is_copy_constructible_v<Placement> && !std::is_copy_assignable_v<Placement>,
              "a placement proof owns its region, so a copy must be refused.");
static_assert(std::is_nothrow_move_constructible_v<Placement> && std::is_nothrow_move_assignable_v<Placement>);
static_assert(!std::is_trivially_copyable_v<Placement> && !std::is_implicit_lifetime_v<Placement>
                  && !std::is_aggregate_v<Placement>,
              "std::bit_cast and std::start_lifetime_as must not build a placement proof.");

// The share mode is part of the region type, so a shared or a file
// region has no path to the mint.
static_assert(std::is_same_v<Region::share_type, mmap::share::Anonymous>);

using IoBlockCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Test,
    ::foundation::effects::Row<::foundation::effects::Effect::Test, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>>;
using IoOnlyCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Test,
    ::foundation::effects::Row<::foundation::effects::Effect::Test, ::foundation::effects::Effect::IO>>;
using ForegroundCtx = ::foundation::effects::ExecCtx<>;

static_assert(numa::CtxFitsNumaBind<IoBlockCtx>);
static_assert(!numa::CtxFitsNumaBind<IoOnlyCtx>,
              "a context without Block must not bind memory: mbind can park the caller while it moves pages.");
static_assert(!numa::CtxFitsNumaBind<ForegroundCtx>);

}  // namespace fixy::detail::numa_placement_invariants
