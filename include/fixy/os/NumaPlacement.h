#pragma once

// Proof that an anonymous memory region is bound to one NUMA node.
//
// The proof owns the region.  Every constructor that builds one is
// private, and the sole friend is fixy::numa::mint_numa_placement.  That
// mint calls mbind(2) with MPOL_BIND for one concrete node, and it builds
// a proof only when the call succeeded.  So a caller that holds a
// NumaPlacement holds memory whose policy the kernel set to that node.
//
// The mint passes MPOL_MF_STRICT and MPOL_MF_MOVE.  The kernel moves each
// page that the region already holds to the node, and it fails the call
// if a page cannot move.  So success means that every page of the region
// is on the node, and that each page the region allocates later comes
// from the node.
//
// Only an anonymous private region binds.  The kernel ignores the policy
// of a MAP_SHARED range, and a private file range takes its page-cache
// pages from the policy of the thread that reads them.  mbind(2) returns
// success for both, so a proof over such a range would state a binding
// that the kernel does not keep.  The mint takes an OwnedMmap whose share
// mode is share::Anonymous and no other.  That type is a true claim,
// because OwnedMmap::mint_region calculates its MAP_* word from the type
// and no caller gives it a bit.
//
// The proof is move-only.  A move carries the region and the node, and
// the source then covers no node.  The proof gives the address and the
// length of the region, but not the region itself: a mutable reference
// to the region would let a caller move a different region into it and
// keep the claim.  consume() gives the region back and ends the claim.
// The proof carries the brand of the region, so the region that comes
// back has the identity it was minted with.
//
// A placement has two halves.  The affinity half is fixy::CpuPinned,
// which mint_affinity builds after sched_setaffinity.  The node half is
// here.
//
// census_page_nodes asks the kernel where the pages of a range are.  A
// first touch is a preference of the kernel and not a guarantee: when
// the node of the CPU has fewer free pages than its low watermark, the
// kernel takes the pages from another node and reports no error.  So a
// caller that touches a range to place it asks the census afterwards.
// The census reads the kernel and changes nothing, and its body is in
// src/fixy/os/NumaPlacement.cpp.  fixy/os/NumaCounters.h gives the
// allocation counters of a node, which show when the kernel fell back.

#include <fixy/Core.h>
#include <fixy/os/OwnedMmap.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/algebra/lattices/NumaNodeLattice.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Values.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MemoryPolicyWords.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy {

using ::foundation::algebra::lattices::is_concrete_numa_node;
using ::foundation::algebra::lattices::NumaNodeId;
using ::foundation::algebra::lattices::NumaNodeLattice;

// The claim a NumaPlacement makes that no lattice grades.
// foundation/diag/RowHash.h folds the identity, so the proof takes a
// cache slot of its own.  The node is a value of each proof, not of its
// type, so the identity does not name it.
namespace row_discipline {
template <typename Prot>
struct numa_placement;
// The claim of a policy proof: the policy, which is a part of its type.
template <auto Policy, typename Prot>
struct numa_policy_placement;
}  // namespace row_discipline

template <typename Tag, typename Prot, typename Brand = ::foundation::brand::DefaultBrand>
class NumaPlacement;

// The region a proof owns.  The share mode is fixed, because the kernel
// keeps a binding for an anonymous private range only.
template <typename Tag, typename Prot, typename Brand = ::foundation::brand::DefaultBrand>
using NumaBindableRegion = OwnedMmap<Tag, Prot, mmap::share::Anonymous, Brand>;

// The count of the concrete nodes: NumaNodeId names the nodes 0 thru 253.
inline constexpr std::size_t numa_concrete_node_count = static_cast<std::size_t>(NumaNodeId::None);

// A set of concrete NUMA nodes: the node mask of a memory policy of mbind(2).
// A sentinel node, None or Any, names no node of the machine, so the mask
// holds none, and insert refuses it with einval.  The slot of a node is its
// id, so a walk of slots() gives the nodes in the order of their ids.
class NumaNodeMask final {
public:
    using Slot = Index<numa_concrete_node_count>;

    // The empty mask.
    constexpr NumaNodeMask() noexcept = default;

    // Puts node into the mask.  A sentinel node gives einval and changes
    // nothing.
    [[nodiscard]] constexpr Result<Unit, ::foundation::os::Errno> insert(NumaNodeId node) & noexcept {
        for (Slot const slot : Slot::check(static_cast<std::size_t>(node))) {
            slots_.set(slot);
            return Unit{};
        }
        return err(::foundation::os::Errno::einval);
    }

    // True when the mask holds node.  A sentinel node is never in a mask.
    [[nodiscard]] constexpr bool contains(NumaNodeId node) const noexcept {
        for (Slot const slot : Slot::check(static_cast<std::size_t>(node))) {
            return slots_.test(slot);
        }
        return false;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return slots_.size(); }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return slots_.is_empty(); }

    // A copy of the slots of the nodes of the mask.  numa_node_of_slot gives
    // the node of a slot.  The copy is 32 bytes, and no reference to the
    // storage of the mask goes out.
    [[nodiscard]] constexpr BitSet<numa_concrete_node_count> slots() const noexcept { return slots_; }

    [[nodiscard]] friend constexpr bool operator==(NumaNodeMask const& left, NumaNodeMask const& right) noexcept {
        return left.slots_ == right.slots_;
    }

private:
    BitSet<numa_concrete_node_count> slots_{};
};

// The node of a slot of a NumaNodeMask.  A slot is below the count of the
// concrete nodes, so its node is concrete.
[[nodiscard]] constexpr NumaNodeId numa_node_of_slot(NumaNodeMask::Slot slot) noexcept {
    return static_cast<NumaNodeId>(slot.raw());
}

// The mask of the one given node, or no value for a sentinel node.  The
// function is not a member of NumaNodeMask, because an Option of a class
// needs the whole class.
[[nodiscard]] constexpr Option<NumaNodeMask> make_numa_node_mask(NumaNodeId node) noexcept {
    NumaNodeMask mask{};
    return mask.insert(node).match(
        [&mask](Unit) noexcept -> Option<NumaNodeMask> { return Option<NumaNodeMask>::some(mask); },
        [](::foundation::os::Errno) noexcept -> Option<NumaNodeMask> { return none; });
}

// The memory policy of a range (mbind(2)), over a node mask.
enum class NumaPolicy : std::uint8_t {
    // Each page on a node of the mask, and on no other node (MPOL_BIND).
    bind,
    // The pages on the nodes of the mask in turn, one page on each node, or
    // on another node when that node is low on free memory (MPOL_INTERLEAVE).
    interleave,
    // Each page on the one node of the mask, or on another node when that
    // node is low on free memory (MPOL_PREFERRED).
    preferred,
};

template <NumaPolicy Policy, typename Tag, typename Prot, typename Brand = ::foundation::brand::DefaultBrand>
class NumaPolicyPlacement;

namespace numa {

// mbind(2) is a memory-policy call of the same kind as madvise(2).  It
// moves pages, so it can park the caller, and the gate is the gate of
// fixy::mmap::advise: the context must own IO and Block.
template <typename Ctx>
concept CtxFitsNumaBind =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

// Declared here and defined below the class, because the class names it
// as its sole friend, and a friend must already have been declared.
template <typename Tag, typename Prot, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaBind<Ctx>
[[nodiscard]] ::foundation::core::Result<::fixy::NumaPlacement<Tag, Prot, Brand>, ::foundation::os::Errno>
mint_numa_placement(Ctx const&, ::fixy::Linear<::fixy::NumaBindableRegion<Tag, Prot, Brand>>&& region,
                    NumaNodeId node) noexcept;

// Declared here and defined below its class, for the same reason.
template <NumaPolicy Policy, typename Tag, typename Prot, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaBind<Ctx>
[[nodiscard]] ::foundation::core::Result<::fixy::NumaPolicyPlacement<Policy, Tag, Prot, Brand>, ::foundation::os::Errno>
mint_numa_policy(Ctx const&, ::fixy::Linear<::fixy::NumaBindableRegion<Tag, Prot, Brand>>&& region,
                 NumaNodeMask const& nodes) noexcept;

}  // namespace numa

namespace detail {

// The width of the node mask that mbind reads.  NumaNodeId names the
// nodes 0 thru 253, so 256 bits hold each of them.  The check file of
// this header pins that each concrete node has a bit.
inline constexpr std::size_t numa_mask_bits = 256;
inline constexpr std::size_t numa_mask_word_bits = sizeof(unsigned long) * CHAR_BIT;

// Memory policy door.  Each function issues its system call through
// foundation::os::raw_syscall, and gives the error of the kernel.  The bodies
// are in src/fixy/os/NumaPlacement.cpp, which includes no header of the
// kernel and no header of the C library.  A function that issues one system
// call name out of line has the name name_sys (the affinity door of
// fixy/os/CpuPinned.h).

// Sets the given policy over the given nodes for the pages of the range,
// and moves the pages that the range holds (MPOL_MF_STRICT and MPOL_MF_MOVE).
// An empty mask gives einval and makes no system call.
//
// This helper is reachable, and that is deliberate: a call sets a policy
// and gives back a result.  It mints nothing.  mint_numa_placement and
// mint_numa_policy build the proof on the success of this helper, and
// nothing else builds one.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mbind_sys(void* address, std::size_t length, NumaPolicy policy, NumaNodeMask const& nodes) noexcept;

// Sets the policy Policy over nodes for the range, as mbind_sys does.  An
// empty mask and a preferred policy over more than one node give einval, and
// the call makes no system call.  mint_numa_policy and the region of a
// LockedMmap (fixy/os/LockedMmap.h) set a policy through this helper.
template <NumaPolicy Policy>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_range_policy_(void* address, std::size_t length, NumaNodeMask const& nodes) noexcept {
    bool const is_one_node_policy = Policy == NumaPolicy::preferred;
    if (nodes.is_empty() || (is_one_node_policy && nodes.size() != 1)) {
        return ::foundation::core::err(::foundation::os::Errno::einval);
    }
    return mbind_sys(address, length, Policy, nodes);
}

}  // namespace detail

template <typename Tag, typename Prot, typename Brand>
class [[nodiscard]] NumaPlacement {
public:
    using region_type = NumaBindableRegion<Tag, Prot, Brand>;
    using row_discipline = ::fixy::row_discipline::numa_placement<Prot>;
    using row_payload = ::foundation::diag::row_payloads<>;

private:
    region_type region_;
    NumaNodeId node_ = NumaNodeId::None;

    // The only constructor that builds a proof, and it is private.
    NumaPlacement(region_type region, NumaNodeId node) noexcept : region_{std::move(region)}, node_{node} {}

    // The sole friend, and the whole gate.  It builds a proof only after
    // detail::mbind_sys succeeded for this region and this node.
    //
    // Keep this friend a function that makes and checks the call.  A friend
    // that only forwards its arguments proves nothing.  The trailing return
    // type is necessary: fixy/os/CpuPinned.h gives the parse reason.
    template <typename FriendTag, typename FriendProt, typename FriendBrand, ::foundation::effects::IsExecCtx FriendCtx>
        requires ::fixy::numa::CtxFitsNumaBind<FriendCtx>
    friend auto ::fixy::numa::mint_numa_placement(
        FriendCtx const&, ::fixy::Linear<::fixy::NumaBindableRegion<FriendTag, FriendProt, FriendBrand>>&&,
        NumaNodeId) noexcept -> ::foundation::core::Result<::fixy::NumaPlacement<FriendTag, FriendProt, FriendBrand>,
                                                           ::foundation::os::Errno>;

public:
    NumaPlacement() = delete("a default-constructed NumaPlacement would claim a binding that nobody made.  Take one "
                             "from fixy::numa::mint_numa_placement, which returns it only after mbind succeeded.");
    NumaPlacement(const NumaPlacement&) = delete("a placement proof owns its region, and a copy would unmap it twice.");
    NumaPlacement& operator=(const NumaPlacement&) = delete("a placement proof owns its region.");

    // User-provided, so the class is neither trivially copyable nor an
    // implicit-lifetime type, and the source of a move covers no node.
    NumaPlacement(NumaPlacement&& other) noexcept
        : region_{std::move(other.region_)}, node_{std::exchange(other.node_, NumaNodeId::None)} {}

    NumaPlacement& operator=(NumaPlacement&& other) noexcept {
        if (this != &other) {
            region_ = std::move(other.region_);
            node_ = std::exchange(other.node_, NumaNodeId::None);
        }
        return *this;
    }

    ~NumaPlacement() = default;

    // The node that the kernel bound the region to.  None after a move.
    [[nodiscard]] NumaNodeId node() const noexcept { return node_; }

    // A borrow.  The proof keeps the region, so the caller must not unmap
    // the returned address.
    [[nodiscard]] void* data() const noexcept { return region_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return region_.size(); }

    // True when this binding covers the requested node: the request is
    // the node itself, or None, which asks for no node.
    [[nodiscard]] bool admits(NumaNodeId request) const noexcept { return NumaNodeLattice::leq(request, node_); }

    // Gives the region back and ends the claim.  The kernel keeps the
    // policy on the range, but no proof states it after this call.
    [[nodiscard]] Linear<region_type> consume() && noexcept {
        node_ = NumaNodeId::None;
        return mint_linear<region_type>(std::move(region_));
    }
};

// Proof that an anonymous memory region has the memory policy Policy over a
// node mask: each page on a node of the mask (bind), the pages on the nodes
// of the mask in turn while each node has free memory (interleave), or each
// page on the one node of the mask while that node has free memory
// (preferred).  The kernel counts each page that went to another node in
// numa_foreign of its node (fixy/os/NumaCounters.h).
//
// The proof owns the region, as NumaPlacement does.  Every constructor that
// builds one is private, and the sole friend is
// fixy::numa::mint_numa_policy, which builds a proof only after mbind(2)
// succeeded for the region and the mask.  The policy is a part of the type,
// and the mask is a value of each proof.  A move carries the region and the
// mask, and the source then covers no node.
template <NumaPolicy Policy, typename Tag, typename Prot, typename Brand>
class [[nodiscard]] NumaPolicyPlacement {
public:
    using region_type = NumaBindableRegion<Tag, Prot, Brand>;
    using row_discipline = ::fixy::row_discipline::numa_policy_placement<Policy, Prot>;
    using row_payload = ::foundation::diag::row_payloads<>;
    static constexpr NumaPolicy policy = Policy;

private:
    region_type region_;
    NumaNodeMask nodes_{};

    // The only constructor that builds a proof, and it is private.
    NumaPolicyPlacement(region_type region, NumaNodeMask const& nodes) noexcept
        : region_{std::move(region)}, nodes_{nodes} {}

    // The sole friend, and the whole gate.  It builds a proof only after
    // detail::mbind_sys succeeded for this region and this mask.  The
    // trailing return type is necessary: fixy/os/CpuPinned.h gives the parse
    // reason.
    template <NumaPolicy FriendPolicy, typename FriendTag, typename FriendProt, typename FriendBrand,
              ::foundation::effects::IsExecCtx FriendCtx>
        requires ::fixy::numa::CtxFitsNumaBind<FriendCtx>
    friend auto ::fixy::numa::mint_numa_policy(
        FriendCtx const&, ::fixy::Linear<::fixy::NumaBindableRegion<FriendTag, FriendProt, FriendBrand>>&&,
        NumaNodeMask const&) noexcept
        -> ::foundation::core::Result<::fixy::NumaPolicyPlacement<FriendPolicy, FriendTag, FriendProt, FriendBrand>,
                                      ::foundation::os::Errno>;

public:
    NumaPolicyPlacement() = delete("a default-constructed policy proof would claim a policy that nobody set.  Take "
                                   "one from fixy::numa::mint_numa_policy, which returns it only after mbind "
                                   "succeeded.");
    NumaPolicyPlacement(const NumaPolicyPlacement&) =
        delete("a policy proof owns its region, and a copy would unmap it twice.");
    NumaPolicyPlacement& operator=(const NumaPolicyPlacement&) = delete("a policy proof owns its region.");

    // User-provided, so the class is neither trivially copyable nor an
    // implicit-lifetime type, and the source of a move covers no node.
    NumaPolicyPlacement(NumaPolicyPlacement&& other) noexcept
        : region_{std::move(other.region_)}, nodes_{::foundation::core::exchange(other.nodes_, NumaNodeMask{})} {}

    NumaPolicyPlacement& operator=(NumaPolicyPlacement&& other) noexcept {
        if (this != &other) {
            region_ = std::move(other.region_);
            nodes_ = ::foundation::core::exchange(other.nodes_, NumaNodeMask{});
        }
        return *this;
    }

    ~NumaPolicyPlacement() = default;

    // A copy of the nodes of the policy.  The empty mask after a move.
    [[nodiscard]] NumaNodeMask nodes() const noexcept { return nodes_; }

    // A borrow.  The proof keeps the region, so the caller must not unmap
    // the returned address.
    [[nodiscard]] void* data() const noexcept { return region_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return region_.size(); }

    // Gives the region back and ends the claim.  The kernel keeps the
    // policy on the range, but no proof states it after this call.
    [[nodiscard]] Linear<region_type> consume() && noexcept {
        nodes_ = NumaNodeMask{};
        return mint_linear<region_type>(std::move(region_));
    }
};

namespace numa {

template <typename Tag, typename Prot, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaBind<Ctx>
[[nodiscard]] ::foundation::core::Result<::fixy::NumaPlacement<Tag, Prot, Brand>, ::foundation::os::Errno>
mint_numa_placement(Ctx const&, ::fixy::Linear<::fixy::NumaBindableRegion<Tag, Prot, Brand>>&& region,
                    NumaNodeId node) noexcept {
    // The region leaves the Linear first, so each exit below ends its
    // life exactly one time: a refusal unmaps it, and success moves it
    // into the proof.  A node that is not concrete and a region with no
    // mapping give einval, and the mint makes no system call.
    ::fixy::NumaBindableRegion<Tag, Prot, Brand> owned = std::move(region).consume();
    CRUCIBLE_TRY(nodes, make_numa_node_mask(node).ok_or(::foundation::os::Errno::einval));
    if (!owned.is_mapped()) {
        return ::foundation::core::err(::foundation::os::Errno::einval);
    }
    ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> const bound =
        ::fixy::detail::mbind_sys(owned.data(), owned.size(), NumaPolicy::bind, nodes);
    for (::foundation::os::Errno const error : bound.err()) {
        return ::foundation::core::err(error);
    }
    return ::fixy::NumaPlacement<Tag, Prot, Brand>{std::move(owned), node};
}

// Sets the policy Policy over nodes for the region, and moves the pages that
// the region holds to the nodes of the policy.  An empty mask, a preferred
// policy over more than one node and a region with no mapping give einval,
// and the mint makes no system call.  A node that the cpuset of the process
// does not allow, or that the machine does not have, gives the error of the
// kernel.
template <NumaPolicy Policy, typename Tag, typename Prot, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaBind<Ctx>
[[nodiscard]] ::foundation::core::Result<::fixy::NumaPolicyPlacement<Policy, Tag, Prot, Brand>, ::foundation::os::Errno>
mint_numa_policy(Ctx const&, ::fixy::Linear<::fixy::NumaBindableRegion<Tag, Prot, Brand>>&& region,
                 NumaNodeMask const& nodes) noexcept {
    // The region leaves the Linear first, as in mint_numa_placement.
    ::fixy::NumaBindableRegion<Tag, Prot, Brand> owned = std::move(region).consume();
    if (!owned.is_mapped()) {
        return ::foundation::core::err(::foundation::os::Errno::einval);
    }
    ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> const set =
        ::fixy::detail::set_range_policy_<Policy>(owned.data(), owned.size(), nodes);
    for (::foundation::os::Errno const error : set.err()) {
        return ::foundation::core::err(error);
    }
    return ::fixy::NumaPolicyPlacement<Policy, Tag, Prot, Brand>{std::move(owned), nodes};
}

// ── The census of the pages of a range ─────────────────────────────────

// The step of a NUMA query that failed.
enum class NumaQueryStep : std::uint8_t {
    PageQuery = 0,  // move_pages(2) refused the query of the pages
    CounterPath = 1,  // the counter file of the node has no valid path
    CounterRead = 2,  // the file door does not read the counter file: the open or a read failed
    CounterFormat = 3,  // the counter file does not hold each counter
    NodeNotConcrete = 4,  // the node is None or Any, which name no counter file
    AllowedRead = 5,  // the file door does not read /proc/self/status
    AllowedFormat = 6,  // the status holds no node list of the allowed nodes
};

// The refusal of a NUMA query: the step that failed, and the errno that
// the kernel gave for it.  The errno is 0 when the step made no call.
struct NumaQueryError {
    NumaQueryStep step = NumaQueryStep::PageQuery;
    int error_number = 0;
};

// The concrete node that the index names, or no value for an index that
// no NumaNodeId names.  fixy::topology::Topology names a node by an
// int, so a caller that has a node of the topology gets its id here.
[[nodiscard]] constexpr Option<NumaNodeId> concrete_numa_node(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(std::to_underlying(NumaNodeId::None))) {
        return none;
    }
    return Option<NumaNodeId>::some(static_cast<NumaNodeId>(index));
}

class PageNodeCensus;

}  // namespace numa

namespace detail {

// Counts into census the node of each page that holds one of the `bytes`
// bytes that start at `first`, or gives the error of the query that the
// kernel refused (the memory policy door).  census_page_nodes is its caller.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
census_page_nodes_(void const* first, std::size_t bytes, numa::PageNodeCensus& census) noexcept;

}  // namespace detail

namespace numa {

// The count of the pages of a range on each node.  One query of the
// kernel gives the node of each page, so the census holds the count of
// each node at the cost of the count of one node.  A caller that placed
// a range on a node reads pages_on and pages_off of that node, and a
// caller that looks for the node of most of the range reads each count.
//
// The census covers each page that holds a byte of the range, so a range
// that is not page-aligned counts its first page and its last page.  A
// page with no node is a page that the range maps with no frame yet, the
// shared zero page of a range that was only read, or a page on a node
// past 253, which no NumaNodeId names.
class PageNodeCensus {
public:
    // The census of an empty range, with no page.
    constexpr PageNodeCensus() noexcept = default;

    // The count of the pages that hold a byte of the range.
    [[nodiscard]] constexpr std::size_t pages() const noexcept { return pages_; }

    // The count of the pages on the node.  0 for None and Any.
    [[nodiscard]] constexpr std::size_t pages_on(NumaNodeId node) const noexcept {
        for (NodeSlot const slot : NodeSlot::check(std::to_underlying(node))) {
            return on_node_.at(slot);
        }
        return 0;
    }

    // The count of the pages on a node other than the node.
    [[nodiscard]] constexpr std::size_t pages_off(NumaNodeId node) const noexcept {
        return pages_ - without_node_ - pages_on(node);
    }

    // The count of the pages that no NumaNodeId names a node for.
    [[nodiscard]] constexpr std::size_t pages_without_node() const noexcept { return without_node_; }

    // True when the range has a page, and each page is on the node.
    [[nodiscard]] constexpr bool is_all_on(NumaNodeId node) const noexcept {
        return pages_ != 0 && pages_on(node) == pages_;
    }

private:
    friend auto detail::census_page_nodes_(void const*, std::size_t, PageNodeCensus&) noexcept
        -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>;

    // Counts one page with the status that move_pages gave it: the node
    // of the page, or a negative errno when the page has no node.
    constexpr void count_page_(int status) noexcept {
        ++pages_;
        if (status >= 0) {
            for (NodeSlot const slot : NodeSlot::check(static_cast<std::size_t>(status))) {
                on_node_.set(slot, on_node_.at(slot) + 1);
                return;
            }
        }
        ++without_node_;
    }

    static constexpr std::size_t concrete_node_count_ = std::to_underlying(NumaNodeId::None);

    // The slot of a concrete node: an index below the count of the
    // concrete nodes.
    using NodeSlot = Index<concrete_node_count_>;

    FixedArray<std::size_t, concrete_node_count_> on_node_{};
    std::size_t without_node_ = 0;
    std::size_t pages_ = 0;
};

// The query takes the read lock of the address space, so it can park the
// caller, and the gate is the gate of the binding: IO and Block.
template <typename Ctx>
concept CtxFitsNumaQuery =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

// The node of each page that holds a byte of the range, as a census.  The
// kernel refuses the query only for a reason of the machine, such as a
// kernel with no NUMA support, and the error then holds its errno.
//
// Not a mint: the census reads the kernel and synthesizes no authority.
// Complexity: one query of the kernel for each 512 pages, O(pages).
template <typename T, std::size_t Extent, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaQuery<Ctx>
[[nodiscard]] Result<PageNodeCensus, NumaQueryError> census_page_nodes(Ctx const&, View<T, Extent> range) noexcept {
    PageNodeCensus census{};
    int const error = ::foundation::os::error_number_of(::fixy::detail::census_page_nodes_(
        first_of_(::foundation::core::detail::BaseLayerRead{}, range), range.size() * sizeof(T), census));
    if (error != 0) {
        return err(NumaQueryError{NumaQueryStep::PageQuery, error});
    }
    return census;
}

// ── The nodes that the process may use ─────────────────────────────────

// The nodes of a node list of the kernel, such as "0-1,4": each range
// first-last and each single node, between commas, up to the end of the text
// or the first newline.  An empty list, a part that is no number, a range
// that runs backward and a node past 253 give no value.  Complexity: linear
// in the text and in the nodes of its ranges.
[[nodiscard]] constexpr Option<NumaNodeMask> parse_numa_node_list(TextView text) noexcept {
    NumaNodeMask nodes{};
    std::size_t value = 0;
    std::size_t range_first = 0;
    bool has_digit = false;
    bool is_range = false;
    bool is_refused = false;
    // Puts the part that a comma or the end closes into the mask.
    auto const put_part = [&]() noexcept {
        std::size_t const first = is_range ? range_first : value;
        if (!has_digit || first > value || value >= numa_concrete_node_count) {
            is_refused = true;
            return;
        }
        for (std::size_t node = first; node <= value; ++node) {
            nodes.insert(static_cast<NumaNodeId>(node)).expect("a node below 254 is concrete");
        }
        value = 0;
        has_digit = false;
        is_range = false;
    };
    for (char const character : text) {
        if (character == '\n' || is_refused) {
            break;
        }
        if (character >= '0' && character <= '9') {
            value = value * 10 + static_cast<std::size_t>(character - '0');
            has_digit = true;
            is_refused = value >= numa_concrete_node_count;
        } else if (character == '-' && has_digit && !is_range) {
            range_first = value;
            value = 0;
            has_digit = false;
            is_range = true;
        } else if (character == ',') {
            put_part();
        } else {
            is_refused = true;
        }
    }
    if (!is_refused) {
        put_part();
    }
    if (is_refused) {
        return none;
    }
    return Option<NumaNodeMask>::some(nodes);
}

// The nodes of the line Mems_allowed_list of the text of /proc/self/status:
// the nodes that the cpuset of the process allows.  A status with no such
// line, or with a list that parse_numa_node_list refuses, gives no value.
// Complexity: linear in the text.
[[nodiscard]] constexpr Option<NumaNodeMask> allowed_numa_nodes_of_status(TextView status) noexcept {
    constexpr TextView key{"\nMems_allowed_list:"};
    for (std::size_t const at : find(status, key)) {
        TextView rest =
            status.window(at + key.size(), status.size() - at - key.size()).expect("the key is inside the status");
        while (rest.size() != 0 && (starts_with(rest, TextView{"\t"}) || starts_with(rest, TextView{" "}))) {
            rest = rest.window(1, rest.size() - 1).expect("a text that is not empty has a tail");
        }
        return parse_numa_node_list(rest);
    }
    return none;
}

}  // namespace numa

namespace detail {

// Reads /proc/self/status, and gives the nodes of its line
// Mems_allowed_list, or the step that failed.  The body is in
// src/fixy/os/NumaPlacement.cpp.  read_allowed_numa_nodes is its caller.
[[nodiscard]] Result<NumaNodeMask, numa::NumaQueryError> read_allowed_numa_nodes_() noexcept;

}  // namespace detail

namespace numa {

// The nodes that the cpuset of the process allows.  An mbind to a node
// outside the set fails, so a caller that chooses the nodes of a policy reads
// this set first.  The read changes nothing.  Not a mint: it reads the kernel
// and synthesizes no authority.  The gate is the gate of the other queries.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaQuery<Ctx>
[[nodiscard]] Result<NumaNodeMask, NumaQueryError> read_allowed_numa_nodes(Ctx const&) noexcept {
    return ::fixy::detail::read_allowed_numa_nodes_();
}

// Thread policy
//   The memory policy of a thread places each page that the thread touches
//   first and that no policy of a range covers (set_mempolicy(2)).  A thread
//   that fork or clone makes takes the policy of its parent, and execve keeps
//   it.  So a launcher such as numactl --interleave gives its policy to each
//   worker.  read_thread_memory_policy reads the policy of the calling
//   thread, and set_thread_memory_policy sets it.  A caller that changes the
//   policy for a time reads it first and sets it back after.

// The kind of the memory policy of a thread.
enum class ThreadPolicyKind : std::uint8_t {
    // No policy of the thread: each page on the node of the CPU that first
    // touches it (MPOL_DEFAULT).  The policy has no nodes.
    first_touch,
    // Each page on a node of the mask, and on no other node (MPOL_BIND).
    bind,
    // The pages on the nodes of the mask in turn (MPOL_INTERLEAVE).
    interleave,
    // Each page on the one node of the mask, or on another node when that
    // node is low on free memory (MPOL_PREFERRED).
    preferred,
};

// The memory policy of a thread: its kind and its nodes.  first_touch has
// no nodes, and each other kind has at least one.
struct ThreadMemoryPolicy final {
    ThreadPolicyKind kind = ThreadPolicyKind::first_touch;
    NumaNodeMask nodes{};

    [[nodiscard]] friend constexpr bool operator==(ThreadMemoryPolicy const& left,
                                                   ThreadMemoryPolicy const& right) noexcept {
        return left.kind == right.kind && left.nodes == right.nodes;
    }
};

// The kind of the policy word that get_mempolicy gives, or no value for
// a word that the door does not name: a policy of a later kernel, such as
// MPOL_LOCAL or MPOL_WEIGHTED_INTERLEAVE, or a word with a mode flag.
[[nodiscard]] constexpr Option<ThreadPolicyKind> thread_policy_kind_of(int word) noexcept {
    switch (word) {
        case ::foundation::os::kMemoryPolicyDefault:
            return Option<ThreadPolicyKind>::some(ThreadPolicyKind::first_touch);
        case ::foundation::os::kMemoryPolicyBind:
            return Option<ThreadPolicyKind>::some(ThreadPolicyKind::bind);
        case ::foundation::os::kMemoryPolicyInterleave:
            return Option<ThreadPolicyKind>::some(ThreadPolicyKind::interleave);
        case ::foundation::os::kMemoryPolicyPreferred:
            return Option<ThreadPolicyKind>::some(ThreadPolicyKind::preferred);
        default:
            return none;
    }
}

// True when policy is a policy that set_mempolicy takes: first_touch with no
// nodes, preferred with one node, and bind or interleave with at least one.
[[nodiscard]] constexpr bool is_settable_thread_policy(ThreadMemoryPolicy const& policy) noexcept {
    switch (policy.kind) {
        case ThreadPolicyKind::first_touch:
            return policy.nodes.is_empty();
        case ThreadPolicyKind::preferred:
            return policy.nodes.size() == 1;
        case ThreadPolicyKind::bind:
        case ThreadPolicyKind::interleave:
            return !policy.nodes.is_empty();
        default:
            return false;
    }
}

// get_mempolicy and set_mempolicy are calls of the memory-policy family, as
// mbind is, so the gate is the gate of the other memory-policy doors: the
// context must own IO and Block.
template <typename Ctx>
concept CtxFitsThreadPolicy =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

}  // namespace numa

namespace detail {

// Reads the memory policy of the calling thread with get_mempolicy.  A word
// that thread_policy_kind_of does not name gives eopnotsupp, and a node past
// the nodes that NumaNodeId names gives eoverflow.  The body is in
// src/fixy/os/NumaPlacement.cpp.
[[nodiscard]] Result<numa::ThreadMemoryPolicy, ::foundation::os::Errno> get_mempolicy_sys() noexcept;

// Sets the memory policy of the calling thread with set_mempolicy.  A policy
// that is_settable_thread_policy refuses gives einval and makes no system
// call.  The body is in src/fixy/os/NumaPlacement.cpp.
[[nodiscard]] Result<Unit, ::foundation::os::Errno> set_mempolicy_sys(numa::ThreadMemoryPolicy const& policy) noexcept;

}  // namespace detail

namespace numa {

// The memory policy of the calling thread (the thread policy).  A host with
// more nodes than NumaNodeId names gives the error of the kernel, einval.
// Not a mint: it reads the kernel and synthesizes no authority.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsThreadPolicy<Ctx>
[[nodiscard]] Result<ThreadMemoryPolicy, ::foundation::os::Errno> read_thread_memory_policy(Ctx const&) noexcept {
    return ::fixy::detail::get_mempolicy_sys();
}

// Sets the memory policy of the calling thread to policy (the thread
// policy).  A policy that is_settable_thread_policy refuses gives einval, and
// a node that the cpuset of the process does not allow gives the error of the
// kernel, einval.  The policy holds for the pages that the thread touches
// first after the call.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsThreadPolicy<Ctx>
[[nodiscard]] Result<Unit, ::foundation::os::Errno>
set_thread_memory_policy(Ctx const&, ThreadMemoryPolicy const& policy) noexcept {
    return ::fixy::detail::set_mempolicy_sys(policy);
}

}  // namespace numa

// A region tag that only checks name.  The witness roster
// (the witness rows of utils/rule-table.txt) and the check file of this header
// spell a placement over it, and no code builds a region of it.
namespace detail::numa_placement_witness {
struct ProbeRegion final {};
}  // namespace detail::numa_placement_witness

}  // namespace fixy
