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
#include <fixy/FixedArray.h>
#include <fixy/OwnedMmap.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/algebra/lattices/NumaNodeLattice.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <linux/mempolicy.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <system_error>
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
}  // namespace row_discipline

template <typename Tag, typename Prot, typename Brand = ::foundation::brand::DefaultBrand>
class NumaPlacement;

// The region a proof owns.  The share mode is fixed, because the kernel
// keeps a binding for an anonymous private range only.
template <typename Tag, typename Prot, typename Brand = ::foundation::brand::DefaultBrand>
using NumaBindableRegion = OwnedMmap<Tag, Prot, mmap::share::Anonymous, Brand>;

namespace numa {

// mbind(2) is a memory-policy call of the same kind as madvise(2).  It
// moves pages, so it can park the caller, and the gate is the gate of
// fixy::mmap::advise: the context must own IO and Block.
template <typename Ctx>
concept CtxFitsNumaBind =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

// Declared here and defined below the class, because the class names it
// as its sole friend, and a friend must already have been declared.
//
// §XXI carve-out: cx=alloc — binding memory is a kernel side effect.
template <typename Tag, typename Prot, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaBind<Ctx>
[[nodiscard]] std::expected<::fixy::NumaPlacement<Tag, Prot, Brand>, std::error_code>
mint_numa_placement(Ctx const&, ::fixy::Linear<::fixy::NumaBindableRegion<Tag, Prot, Brand>>&& region,
                    NumaNodeId node) noexcept;

}  // namespace numa

namespace detail {

// The width of the node mask that mbind reads.  NumaNodeId names the
// nodes 0 thru 253, so 256 bits hold each of them.  The check file of
// this header pins that each concrete node has a bit.
inline constexpr std::size_t numa_mask_bits = 256;
inline constexpr std::size_t numa_mask_word_bits = sizeof(unsigned long) * CHAR_BIT;

// Binds the pages of the range to one concrete node, and moves the pages
// that the range holds.  Returns 0 on success and the errno otherwise.
//
// This helper is reachable, and that is deliberate: a call binds memory
// and gives back an int.  It mints nothing.  mint_numa_placement builds
// the proof on the success of this helper, and nothing else builds one.
//
// The kernel decrements the node count that it receives before it reads
// the mask, so the call passes one more than the width of the mask.
[[nodiscard]] inline int bind_range_to_node(void* address, std::size_t length, NumaNodeId node) noexcept {
    unsigned long mask[numa_mask_bits / numa_mask_word_bits]{};
    std::size_t const bit = std::to_underlying(node);
    mask[bit / numa_mask_word_bits] = 1UL << (bit % numa_mask_word_bits);
    long const status = ::syscall(
        SYS_mbind, address, length, MPOL_BIND, mask, numa_mask_bits + 1,
        MPOL_MF_STRICT
            | MPOL_MF_MOVE);  // SYSCALL-CAP-OK: detail helper for mint_numa_placement ctx-gate (CtxFitsNumaBind: effects::IO+Block)
    return status == 0 ? 0 : errno;
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
    // detail::bind_range_to_node returned 0 for this region and this node.
    //
    // Keep this friend a function that MAKES AND CHECKS the call.  A friend
    // that only forwards its arguments proves nothing.  The trailing return
    // type is necessary: fixy/os/CpuPinned.h gives the parse reason.
    template <typename FriendTag, typename FriendProt, typename FriendBrand, ::foundation::effects::IsExecCtx FriendCtx>
        requires ::fixy::numa::CtxFitsNumaBind<FriendCtx>
    friend auto ::fixy::numa::mint_numa_placement(
        FriendCtx const&, ::fixy::Linear<::fixy::NumaBindableRegion<FriendTag, FriendProt, FriendBrand>>&&,
        NumaNodeId) noexcept
        -> std::expected<::fixy::NumaPlacement<FriendTag, FriendProt, FriendBrand>, std::error_code>;

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

namespace numa {

// §XXI carve-out: cx=alloc — binding memory is a kernel side effect.
template <typename Tag, typename Prot, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsNumaBind<Ctx>
[[nodiscard]] std::expected<::fixy::NumaPlacement<Tag, Prot, Brand>, std::error_code>
mint_numa_placement(Ctx const&, ::fixy::Linear<::fixy::NumaBindableRegion<Tag, Prot, Brand>>&& region,
                    NumaNodeId node) noexcept {
    // The region leaves the Linear first, so each exit below ends its
    // life exactly one time: a refusal unmaps it, and success moves it
    // into the proof.
    ::fixy::NumaBindableRegion<Tag, Prot, Brand> owned = std::move(region).consume();
    if (!is_concrete_numa_node(node) || !owned.is_mapped()) {
        return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
    }
    if (int const error = ::fixy::detail::bind_range_to_node(owned.data(), owned.size(), node); error != 0) {
        return std::unexpected{std::error_code{error, std::system_category()}};
    }
    return ::fixy::NumaPlacement<Tag, Prot, Brand>{std::move(owned), node};
}

// ── The census of the pages of a range ─────────────────────────────────

// The step of a NUMA query that failed.
enum class NumaQueryStep : std::uint8_t {
    PageQuery = 0,  // move_pages(2) refused the query of the pages
    CounterPath = 1,  // the counter file of the node has no valid path
    CounterOpen = 2,  // the counter file of the node does not open
    CounterRead = 3,  // the read of the counter file failed
    CounterFormat = 4,  // the counter file does not hold each counter
    NodeNotConcrete = 5,  // the node is None or Any, which name no counter file
};

// The refusal of a NUMA query: the step that failed, and the errno that
// the kernel gave for it.  The errno is 0 when the step made no call.
struct NumaQueryError {
    NumaQueryStep step = NumaQueryStep::PageQuery;
    int error_number = 0;
};

// The concrete node that the index names, or no value for an index that
// no NumaNodeId names.  fixy::concurrent::Topology names a node by an
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
// bytes that start at `first`.  Returns 0, or the errno of the query that
// the kernel refused.  The body is in src/fixy/os/NumaPlacement.cpp, and
// census_page_nodes is its caller.
[[nodiscard]] int census_page_nodes_(void const* first, std::size_t bytes, numa::PageNodeCensus& census) noexcept;

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
        return is_concrete_numa_node(node) ? on_node_[std::to_underlying(node)] : 0;
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
    friend int detail::census_page_nodes_(void const*, std::size_t, PageNodeCensus&) noexcept;

    // Counts one page with the status that move_pages gave it: the node
    // of the page, or a negative errno when the page has no node.
    constexpr void count_page_(int status) noexcept {
        ++pages_;
        if (status >= 0 && status < static_cast<int>(concrete_node_count_)) {
            ++on_node_[static_cast<std::size_t>(status)];
        } else {
            ++without_node_;
        }
    }

    static constexpr std::size_t concrete_node_count_ = std::to_underlying(NumaNodeId::None);

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
    int const error = ::fixy::detail::census_page_nodes_(::foundation::core::detail::first_of_(range),
                                                         range.size() * sizeof(T), census);
    if (error != 0) {
        return err(NumaQueryError{NumaQueryStep::PageQuery, error});
    }
    return census;
}

}  // namespace numa

// A region tag that only checks name.  The witness roster
// (utils/scripts/witness-roster.txt) and the check file of this header
// spell a placement over it, and no code builds a region of it.
namespace detail::numa_placement_witness {
struct ProbeRegion final {};
}  // namespace detail::numa_placement_witness

}  // namespace fixy
