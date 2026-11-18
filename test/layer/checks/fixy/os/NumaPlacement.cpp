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

// The policy proof has the gate of the bind proof: no route builds one
// with no call to mbind.
using Interleaved = NumaPolicyPlacement<NumaPolicy::interleave, ProbeRegion, mmap::prot::WriteCopy>;
static_assert(!std::is_default_constructible_v<Interleaved> && !std::is_constructible_v<Interleaved, Region>,
              "a policy proof comes only from mint_numa_policy");
static_assert(!std::is_copy_constructible_v<Interleaved> && std::is_nothrow_move_constructible_v<Interleaved>);
static_assert(!std::is_trivially_copyable_v<Interleaved> && !std::is_aggregate_v<Interleaved>,
              "std::bit_cast and aggregate initialization must not build a policy proof");
static_assert(Interleaved::policy == NumaPolicy::interleave);

// The proof gives its nodes as a value, so no reference to its storage goes
// out.
static_assert(std::is_same_v<decltype(std::declval<Interleaved const&>().nodes()), NumaNodeMask>);

}  // namespace fixy::detail::numa_placement_invariants

namespace fixy::detail::numa_mask_invariants {

// THE NODE MASK: a sentinel node is no member.
[[nodiscard]] consteval bool sentinel_insert_is_refused() {
    NumaNodeMask mask{};
    bool const is_refused = mask.insert(NumaNodeId::None).is_err() && mask.insert(NumaNodeId::Any).is_err();
    return is_refused && mask.is_empty() && !mask.contains(NumaNodeId::Any);
}
static_assert(sentinel_insert_is_refused(), "a mask holds no sentinel node");
static_assert(fixy::make_numa_node_mask(NumaNodeId::None).is_none()
              && fixy::make_numa_node_mask(NumaNodeId{253}).is_some());
static_assert(numa_node_of_slot(NumaNodeMask::Slot::check(7).expect("7 is a slot")) == NumaNodeId{7});

// The mask gives its slots as a copy of 32 bytes, so no reference to its
// storage goes out.
static_assert(std::is_same_v<decltype(std::declval<NumaNodeMask const&>().slots()), BitSet<numa_concrete_node_count>>);
static_assert(sizeof(BitSet<numa_concrete_node_count>) == 32);

// The node list of the kernel.
[[nodiscard]] consteval NumaNodeMask nodes_of(TextView text) {
    return numa::parse_numa_node_list(text).expect("the list parses");
}
template <class... Ids>
[[nodiscard]] consteval NumaNodeMask nodes(Ids... ids) {
    NumaNodeMask mask{};
    (mask.insert(static_cast<NumaNodeId>(ids)).expect("each id is concrete"), ...);
    return mask;
}
static_assert(nodes_of("0") == nodes(0) && nodes_of("0-1") == nodes(0, 1));
static_assert(nodes_of("0-1,4\n") == nodes(0, 1, 4) && nodes_of("2,0-1") == nodes(0, 1, 2));
static_assert(nodes_of("253") == nodes(253));
static_assert(numa::parse_numa_node_list("").is_none() && numa::parse_numa_node_list("254").is_none(),
              "an empty list and a node past 253 give no mask");
static_assert(numa::parse_numa_node_list("3-1").is_none() && numa::parse_numa_node_list("0,,1").is_none()
                  && numa::parse_numa_node_list("0-").is_none() && numa::parse_numa_node_list("x").is_none(),
              "a range that runs backward and a part that is no number give no mask");

// The line of the allowed nodes of a planted status.
static_assert(numa::allowed_numa_nodes_of_status("Name:\tcrucible\nMems_allowed:\t00000003\nMems_allowed_list:\t0-1\n"
                                                 "voluntary_ctxt_switches:\t1\n")
                  .expect("the status has the line")
              == nodes(0, 1));
static_assert(numa::allowed_numa_nodes_of_status("Name:\tcrucible\nMems_allowed:\t00000001\n").is_none(),
              "a status with no list of the allowed nodes gives no mask");

}  // namespace fixy::detail::numa_mask_invariants
