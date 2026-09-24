// The NUMA placement proof, run against the kernel.
//
// Each case maps a real anonymous region and asks the mint to bind it.
// A proof that the mint hands back is then checked against the kernel's
// own answer: get_mempolicy(2) must report MPOL_BIND with the one node
// of the proof, and a page that the test wrote must sit on that node.
//
// A kernel without NUMA support answers ENOSYS, and a cpuset that does
// not admit node 0 answers EINVAL or EPERM.  The cases that need a
// binding skip on those answers.  The refusals of the mint itself do not
// depend on the machine, so they run everywhere.

#include <fixy/os/Mmap.h>
#include <fixy/os/NumaPlacement.h>

#include <linux/mempolicy.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <utility>

namespace eff = foundation::effects;

namespace {

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block>>;

struct BoundRegion final {};

using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;

// A case that the machine cannot run returns this, and main skips the
// cases that need a binding.  The fixy tests skip by a message and exit 0.
inline constexpr int kSkip = -1;
inline constexpr fixy::NumaNodeId kNodeZero{0};

[[nodiscard]] std::size_t page_bytes() noexcept { return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)); }

[[nodiscard]] auto map_two_pages(BgCtx const& ctx) noexcept {
    return fixy::mmap::mint_mmap_anon<BoundRegion, WriteAnon, Anonymous>(ctx, 2 * page_bytes());
}

// True when the kernel refused a binding for a reason of the machine, not
// of the call: no NUMA support, or a cpuset that does not admit the node.
[[nodiscard]] bool is_machine_refusal(std::error_code const& error) noexcept {
    return error.value() == ENOSYS || error.value() == EPERM || error.value() == EINVAL;
}

// The policy the kernel keeps for the page at the address.  mode is
// MPOL_BIND and the mask holds only node 0 when the binding took.
struct AddressPolicy {
    int mode = -1;
    unsigned long mask[fixy::detail::numa_mask_bits / fixy::detail::numa_mask_word_bits]{};
    bool is_known = false;
};

[[nodiscard]] AddressPolicy policy_at(void* address) noexcept {
    AddressPolicy policy;
    long const status = ::syscall(SYS_get_mempolicy, &policy.mode, policy.mask, fixy::detail::numa_mask_bits + 1,
                                  address, MPOL_F_ADDR);
    policy.is_known = status == 0;
    return policy;
}

// The node that holds the page at the address, or -1.
[[nodiscard]] int node_of_page(void* address) noexcept {
    int node = -1;
    long const status =
        ::syscall(SYS_get_mempolicy, &node, nullptr, 0UL, address, MPOL_F_NODE | MPOL_F_ADDR);
    return status == 0 ? node : -1;
}

[[nodiscard]] int a_bound_region_is_on_its_node() {
    BgCtx ctx{eff::testing::bg()};
    auto mapped = map_two_pages(ctx);
    if (!mapped) {
        std::fprintf(stderr, "an anonymous mapping of two pages failed (%s)\n", mapped.error().message().c_str());
        return 1;
    }
    auto placed = fixy::numa::mint_numa_placement(ctx, std::move(*mapped), kNodeZero);
    if (!placed) {
        if (is_machine_refusal(placed.error())) {
            std::fprintf(stderr, "[skipped] the kernel refused a binding to node 0 (%s)\n",
                         placed.error().message().c_str());
            return kSkip;
        }
        std::fprintf(stderr, "a binding to node 0 failed (%s)\n", placed.error().message().c_str());
        return 1;
    }
    if (placed->node() != kNodeZero || placed->size() != 2 * page_bytes()) {
        std::fprintf(stderr, "the proof states a node or a length that the mint was not given\n");
        return 1;
    }

    std::memset(placed->data(), 0x5a, placed->size());

    AddressPolicy const policy = policy_at(placed->data());
    if (!policy.is_known || policy.mode != MPOL_BIND) {
        std::fprintf(stderr, "the kernel does not report MPOL_BIND for a region with a proof (mode %d)\n",
                     policy.mode);
        return 1;
    }
    if (policy.mask[0] != 1UL) {
        std::fprintf(stderr, "the kernel reports a mask other than node 0 for the region (%lx)\n", policy.mask[0]);
        return 1;
    }
    for (std::size_t word = 1; word < std::size(policy.mask); ++word) {
        if (policy.mask[word] != 0UL) {
            std::fprintf(stderr, "the kernel reports a node past 63 for the region\n");
            return 1;
        }
    }
    auto* const second_page = static_cast<unsigned char*>(placed->data()) + page_bytes();
    if (node_of_page(placed->data()) != 0 || node_of_page(second_page) != 0) {
        std::fprintf(stderr, "a page that the test wrote is not on node 0\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int the_proof_admits_only_its_node() {
    BgCtx ctx{eff::testing::bg()};
    auto mapped = map_two_pages(ctx);
    if (!mapped) return 1;
    auto placed = fixy::numa::mint_numa_placement(ctx, std::move(*mapped), kNodeZero);
    if (!placed) return is_machine_refusal(placed.error()) ? kSkip : 1;

    if (!placed->admits(kNodeZero) || !placed->admits(fixy::NumaNodeId::None)) {
        std::fprintf(stderr, "a proof over node 0 does not admit node 0\n");
        return 1;
    }
    if (placed->admits(fixy::NumaNodeId{1}) || placed->admits(fixy::NumaNodeId::Any)) {
        std::fprintf(stderr, "a proof over node 0 admits a request that it does not cover\n");
        return 1;
    }

    // A move carries the claim, and the source covers no node after it.
    fixy::NumaPlacement<BoundRegion, fixy::mmap::prot::WriteCopy> moved{std::move(*placed)};
    if (moved.node() != kNodeZero || !moved.admits(kNodeZero)) {
        std::fprintf(stderr, "a move did not carry the binding\n");
        return 1;
    }
    if (placed->admits(kNodeZero) || placed->node() != fixy::NumaNodeId::None) {
        std::fprintf(stderr, "the source of a move still claims a node\n");
        return 1;
    }

    // consume gives the region back, mapped, and ends the claim.
    void* const address = moved.data();
    auto region = std::move(moved).consume();
    if (!region.peek().is_mapped() || region.peek().data() != address) {
        std::fprintf(stderr, "consume did not give back the bound region\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int the_mint_refuses_a_sentinel_node() {
    BgCtx ctx{eff::testing::bg()};
    for (fixy::NumaNodeId const sentinel : {fixy::NumaNodeId::None, fixy::NumaNodeId::Any}) {
        auto mapped = map_two_pages(ctx);
        if (!mapped) return 1;
        auto placed = fixy::numa::mint_numa_placement(ctx, std::move(*mapped), sentinel);
        if (placed || placed.error() != std::errc::invalid_argument) {
            std::fprintf(stderr, "the mint did not refuse a sentinel node with EINVAL\n");
            return 1;
        }
    }
    return 0;
}

[[nodiscard]] int the_mint_refuses_an_empty_region() {
    BgCtx ctx{eff::testing::bg()};
    auto empty = fixy::mint_linear<fixy::NumaBindableRegion<BoundRegion, fixy::mmap::prot::WriteCopy>>();
    auto placed = fixy::numa::mint_numa_placement(ctx, std::move(empty), kNodeZero);
    if (placed || placed.error() != std::errc::invalid_argument) {
        std::fprintf(stderr, "the mint did not refuse a region with no mapping\n");
        return 1;
    }
    return 0;
}

// Node 253 is concrete, and no shipped machine has it.  The kernel refuses
// it, and the mint gives back the kernel's error and no proof.
[[nodiscard]] int the_mint_refuses_a_node_the_machine_lacks() {
    BgCtx ctx{eff::testing::bg()};
    auto mapped = map_two_pages(ctx);
    if (!mapped) return 1;
    auto placed = fixy::numa::mint_numa_placement(ctx, std::move(*mapped), fixy::NumaNodeId{253});
    if (placed) {
        std::fprintf(stderr, "the mint built a proof over node 253\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    int const refusals = the_mint_refuses_a_sentinel_node() + the_mint_refuses_an_empty_region()
                       + the_mint_refuses_a_node_the_machine_lacks();
    if (refusals != 0) return 1;

    int const bound = a_bound_region_is_on_its_node();
    if (bound == kSkip) return 0;
    if (bound != 0) return 1;
    int const admits = the_proof_admits_only_its_node();
    if (admits != 0 && admits != kSkip) return 1;

    std::printf("test_os_numa_placement: a bound region is on its node, and the mint refuses three bad calls\n");
    return 0;
}
