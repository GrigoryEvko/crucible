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
// nodes 0 thru 253, so 256 bits hold each of them.
inline constexpr std::size_t numa_mask_bits = 256;
inline constexpr std::size_t numa_mask_word_bits = sizeof(unsigned long) * CHAR_BIT;
static_assert(numa_mask_bits % numa_mask_word_bits == 0);
static_assert(std::to_underlying(NumaNodeId::None) <= numa_mask_bits,
              "every concrete node must have a bit in the mask that mbind reads");

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

}  // namespace numa

}  // namespace fixy

namespace fixy::detail::numa_placement_invariants {

struct ProbeRegion final {};
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
