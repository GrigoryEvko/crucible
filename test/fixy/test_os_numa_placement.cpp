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
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;

namespace {

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block>>;

// A mapping is minted with the exclusive permission of its region, so the
// region tag declares the row a permission needs.
struct BoundRegion final {
    using permission_row = eff::Row<>;
};

using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;

// A case that the machine cannot run returns this, and main skips the
// cases that need a binding.  The fixy tests skip by a message and exit 0.
inline constexpr int kSkip = -1;
inline constexpr fixy::NumaNodeId kNodeZero{0};

[[nodiscard]] std::size_t page_bytes() noexcept { return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)); }

using foundation::os::Errno;

// The mapping takes the tag and the brand of the permission.  The
// permission ends here, because no case of this test discards pages.  An
// anonymous mapping of two pages does not fail on a host that runs the
// tests.
[[nodiscard]] auto map_two_pages(BgCtx const& ctx) noexcept {
    auto const owner = foundation::permissions::mint_permission_root<BoundRegion>();
    return fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, 2 * page_bytes())
        .expect("an anonymous mapping of two pages succeeds");
}

// The error of a result, or the zero Errno for a value.
template <typename T>
[[nodiscard]] Errno error_of(fixy::Result<T, Errno> const& result) noexcept {
    return result.err().value_or(Errno{});
}

// True when the kernel refused a binding for a reason of the machine, not
// of the call: no NUMA support, or a cpuset that does not admit the node.
[[nodiscard]] bool is_machine_refusal(Errno error) noexcept {
    return error == Errno::enosys || error == Errno::eperm || error == Errno::einval;
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
    long const status =
        ::syscall(SYS_get_mempolicy, &policy.mode, policy.mask, fixy::detail::numa_mask_bits + 1, address, MPOL_F_ADDR);
    policy.is_known = status == 0;
    return policy;
}

// The node that holds the page at the address, or -1.
[[nodiscard]] int node_of_page(void* address) noexcept {
    int node = -1;
    long const status = ::syscall(SYS_get_mempolicy, &node, nullptr, 0UL, address, MPOL_F_NODE | MPOL_F_ADDR);
    return status == 0 ? node : -1;
}

[[nodiscard]] int a_bound_region_is_on_its_node() {
    BgCtx ctx{eff::testing::bg()};
    auto placed = fixy::numa::mint_numa_placement(ctx, map_two_pages(ctx), kNodeZero);
    if (placed.is_err()) {
        if (is_machine_refusal(error_of(placed))) {
            std::fprintf(stderr, "[skipped] the kernel refused a binding to node 0 (errno %d)\n",
                         foundation::os::error_number_of(placed));
            return kSkip;
        }
        std::fprintf(stderr, "a binding to node 0 failed (errno %d)\n", foundation::os::error_number_of(placed));
        return 1;
    }
    auto const placement = std::move(placed).expect("the binding succeeded");
    if (placement.node() != kNodeZero || placement.size() != 2 * page_bytes()) {
        std::fprintf(stderr, "the proof states a node or a length that the mint was not given\n");
        return 1;
    }

    std::memset(placement.data(), 0x5a, placement.size());

    AddressPolicy const policy = policy_at(placement.data());
    if (!policy.is_known || policy.mode != MPOL_BIND) {
        std::fprintf(stderr, "the kernel does not report MPOL_BIND for a region with a proof (mode %d)\n", policy.mode);
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
    auto* const second_page = static_cast<unsigned char*>(placement.data()) + page_bytes();
    if (node_of_page(placement.data()) != 0 || node_of_page(second_page) != 0) {
        std::fprintf(stderr, "a page that the test wrote is not on node 0\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int the_proof_admits_only_its_node() {
    BgCtx ctx{eff::testing::bg()};
    auto placed = fixy::numa::mint_numa_placement(ctx, map_two_pages(ctx), kNodeZero);
    if (placed.is_err()) return is_machine_refusal(error_of(placed)) ? kSkip : 1;
    auto placement = std::move(placed).expect("the binding succeeded");

    if (!placement.admits(kNodeZero) || !placement.admits(fixy::NumaNodeId::None)) {
        std::fprintf(stderr, "a proof over node 0 does not admit node 0\n");
        return 1;
    }
    if (placement.admits(fixy::NumaNodeId{1}) || placement.admits(fixy::NumaNodeId::Any)) {
        std::fprintf(stderr, "a proof over node 0 admits a request that it does not cover\n");
        return 1;
    }

    // A move carries the claim, and the source covers no node after it.
    // The proof carries the brand of the mapping, so its type is read off
    // the mint's result rather than spelled.
    using Placement = decltype(placement);
    static_assert(std::is_same_v<Placement::region_type::tag_type, BoundRegion>);
    static_assert(foundation::brand::IsBranded<Placement::region_type>,
                  "a placement keeps the brand of the mapping it binds");
    Placement moved{std::move(placement)};
    if (moved.node() != kNodeZero || !moved.admits(kNodeZero)) {
        std::fprintf(stderr, "a move did not carry the binding\n");
        return 1;
    }
    if (placement.admits(kNodeZero) || placement.node() != fixy::NumaNodeId::None) {
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
        auto const placed = fixy::numa::mint_numa_placement(ctx, map_two_pages(ctx), sentinel);
        if (error_of(placed) != Errno::einval) {
            std::fprintf(stderr, "the mint did not refuse a sentinel node with EINVAL\n");
            return 1;
        }
    }
    return 0;
}

[[nodiscard]] int the_mint_refuses_an_empty_region() {
    BgCtx ctx{eff::testing::bg()};
    auto empty = fixy::mint_linear<fixy::NumaBindableRegion<BoundRegion, fixy::mmap::prot::WriteCopy>>();
    auto const placed = fixy::numa::mint_numa_placement(ctx, std::move(empty), kNodeZero);
    if (error_of(placed) != Errno::einval) {
        std::fprintf(stderr, "the mint did not refuse a region with no mapping\n");
        return 1;
    }
    return 0;
}

// Node 253 is concrete, and no shipped machine has it.  The kernel refuses
// it, and the mint gives back the kernel's error and no proof.
[[nodiscard]] int the_mint_refuses_a_node_the_machine_lacks() {
    BgCtx ctx{eff::testing::bg()};
    auto const placed = fixy::numa::mint_numa_placement(ctx, map_two_pages(ctx), fixy::NumaNodeId{253});
    if (placed.is_ok()) {
        std::fprintf(stderr, "the mint built a proof over node 253\n");
        return 1;
    }
    return 0;
}

// The nodes that the cpuset of the process allows.  Each process may use one
// node at least.
[[nodiscard]] fixy::NumaNodeMask allowed_nodes(BgCtx const& ctx) noexcept {
    return fixy::numa::read_allowed_numa_nodes(ctx).match(
        [](fixy::NumaNodeMask nodes) noexcept { return nodes; },
        [](fixy::numa::NumaQueryError) noexcept { return fixy::NumaNodeMask{}; });
}

// The census of the pages of a region after the test wrote each byte.  The
// region leaves its proof, and the kernel keeps the policy of the range.
template <class Proof>
[[nodiscard]] fixy::numa::PageNodeCensus census_of(BgCtx const& ctx, Proof&& placement) noexcept {
    auto region = std::move(placement).consume().consume();
    fixy::View<std::byte> const bytes = region.template view<std::byte>().expect("the region is whole pages");
    fixy::fill(bytes, std::byte{0x5a});
    return fixy::numa::census_page_nodes(ctx, bytes).expect("the kernel answers the census of a mapped range");
}

[[nodiscard]] int the_allowed_nodes_are_not_empty() {
    BgCtx ctx{eff::testing::bg()};
    if (allowed_nodes(ctx).is_empty()) {
        fixy::report(fixy::Sink::Err, "the process has no allowed node, or the status gave none\n");
        return 1;
    }
    return 0;
}

// An interleave over the allowed nodes puts the pages of a region on each of
// them in turn, so each allowed node holds a page, and no page is off the
// nodes of the mask.
[[nodiscard]] int an_interleaved_region_spreads_over_its_nodes() {
    BgCtx ctx{eff::testing::bg()};
    fixy::NumaNodeMask const nodes = allowed_nodes(ctx);
    auto placed = fixy::numa::mint_numa_policy<fixy::NumaPolicy::interleave>(ctx, map_two_pages(ctx), nodes);
    if (placed.is_err()) {
        if (is_machine_refusal(error_of(placed))) return kSkip;
        fixy::report(fixy::Sink::Err, "an interleave over the allowed nodes failed (errno {})\n",
                     foundation::os::error_number_of(placed));
        return 1;
    }
    auto placement = std::move(placed).expect("the interleave succeeded");
    if (!(placement.nodes() == nodes)) {
        fixy::report(fixy::Sink::Err, "the interleave proof states a mask that the mint was not given\n");
        return 1;
    }
    fixy::numa::PageNodeCensus const census = census_of(ctx, std::move(placement));
    std::size_t on_mask = 0;
    std::size_t nodes_with_a_page = 0;
    for (fixy::NumaNodeMask::Slot const slot : nodes.slots()) {
        std::size_t const pages = census.pages_on(fixy::numa_node_of_slot(slot));
        on_mask += pages;
        nodes_with_a_page += pages != 0 ? 1 : 0;
    }
    std::size_t const expected_nodes = nodes.size() < 2 ? nodes.size() : 2;
    if (on_mask != census.pages() || nodes_with_a_page != expected_nodes) {
        fixy::report(fixy::Sink::Err, "an interleave put {} of {} pages on its {} nodes, and pages on {} of them\n",
                     on_mask, census.pages(), nodes.size(), nodes_with_a_page);
        return 1;
    }
    return 0;
}

// A preferred policy over one allowed node puts each page on that node while
// the node has free memory, which a region of two pages does not exhaust.
[[nodiscard]] int a_preferred_region_is_on_its_node() {
    BgCtx ctx{eff::testing::bg()};
    fixy::NumaNodeMask const allowed = allowed_nodes(ctx);
    if (allowed.is_empty()) return 1;
    fixy::NumaNodeId node = fixy::NumaNodeId::None;
    for (fixy::NumaNodeMask::Slot const slot : allowed.slots()) {
        node = fixy::numa_node_of_slot(slot);
    }
    fixy::NumaNodeMask const one_node = fixy::make_numa_node_mask(node).expect("an allowed node is concrete");
    auto placed = fixy::numa::mint_numa_policy<fixy::NumaPolicy::preferred>(ctx, map_two_pages(ctx), one_node);
    if (placed.is_err()) {
        if (is_machine_refusal(error_of(placed))) return kSkip;
        fixy::report(fixy::Sink::Err, "a preferred policy over node {} failed (errno {})\n",
                     std::size_t{std::to_underlying(node)}, foundation::os::error_number_of(placed));
        return 1;
    }
    fixy::numa::PageNodeCensus const census = census_of(ctx, std::move(placed).expect("the policy was set"));
    if (!census.is_all_on(node)) {
        fixy::report(fixy::Sink::Err, "a preferred region has {} of its {} pages off node {}\n", census.pages_off(node),
                     census.pages(), std::size_t{std::to_underlying(node)});
        return 1;
    }
    return 0;
}

// The policy mint refuses an empty mask and a preferred policy over two
// nodes before it calls the kernel.
[[nodiscard]] int the_policy_mint_refuses_a_bad_mask() {
    BgCtx ctx{eff::testing::bg()};
    auto const empty =
        fixy::numa::mint_numa_policy<fixy::NumaPolicy::interleave>(ctx, map_two_pages(ctx), fixy::NumaNodeMask{});
    fixy::NumaNodeMask two_nodes{};
    two_nodes.insert(fixy::NumaNodeId{0}).expect("node 0 is concrete");
    two_nodes.insert(fixy::NumaNodeId{1}).expect("node 1 is concrete");
    auto const preferred =
        fixy::numa::mint_numa_policy<fixy::NumaPolicy::preferred>(ctx, map_two_pages(ctx), two_nodes);
    if (error_of(empty) != Errno::einval || error_of(preferred) != Errno::einval) {
        fixy::report(fixy::Sink::Err, "the policy mint did not refuse an empty mask or two preferred nodes\n");
        return 1;
    }
    return 0;
}

// The thread policy door refuses a policy that set_mempolicy does not take
// before it calls the kernel: first_touch with a node, preferred with two
// nodes, and an interleave with no node.
[[nodiscard]] int the_thread_policy_door_refuses_a_bad_policy() {
    using fixy::numa::ThreadMemoryPolicy;
    using fixy::numa::ThreadPolicyKind;
    BgCtx ctx{eff::testing::bg()};
    fixy::NumaNodeMask two_nodes{};
    two_nodes.insert(fixy::NumaNodeId{0}).expect("node 0 is concrete");
    two_nodes.insert(fixy::NumaNodeId{1}).expect("node 1 is concrete");
    fixy::NumaNodeMask const one_node = fixy::make_numa_node_mask(fixy::NumaNodeId{0}).expect("node 0 is concrete");
    auto const touch = fixy::numa::set_thread_memory_policy(
        ctx, ThreadMemoryPolicy{.kind = ThreadPolicyKind::first_touch, .nodes = one_node});
    auto const preferred = fixy::numa::set_thread_memory_policy(
        ctx, ThreadMemoryPolicy{.kind = ThreadPolicyKind::preferred, .nodes = two_nodes});
    auto const empty = fixy::numa::set_thread_memory_policy(
        ctx, ThreadMemoryPolicy{.kind = ThreadPolicyKind::interleave, .nodes = fixy::NumaNodeMask{}});
    if (error_of(touch) != Errno::einval || error_of(preferred) != Errno::einval || error_of(empty) != Errno::einval) {
        fixy::report(fixy::Sink::Err, "the thread policy door did not refuse a policy that the kernel refuses\n");
        return 1;
    }
    return 0;
}

// The thread policy reads back as the door set it.  The case reads the policy
// of the thread, sets an interleave over the allowed nodes, reads it, sets the
// first policy back and reads that.  A policy of a launcher that the door does
// not name skips the case.
[[nodiscard]] int the_thread_policy_reads_back_as_set() {
    using fixy::numa::ThreadMemoryPolicy;
    using fixy::numa::ThreadPolicyKind;
    BgCtx ctx{eff::testing::bg()};
    auto first_read = fixy::numa::read_thread_memory_policy(ctx);
    if (first_read.is_err()) {
        if (error_of(first_read) == Errno::eopnotsupp) return kSkip;
        fixy::report(fixy::Sink::Err, "the read of the thread policy failed (errno {})\n",
                     foundation::os::error_number_of(first_read));
        return 1;
    }
    ThreadMemoryPolicy const first = std::move(first_read).expect("the read gave the policy");
    ThreadMemoryPolicy const interleave{.kind = ThreadPolicyKind::interleave, .nodes = allowed_nodes(ctx)};
    auto const set = fixy::numa::set_thread_memory_policy(ctx, interleave);
    if (set.is_err()) {
        if (is_machine_refusal(error_of(set))) return kSkip;
        fixy::report(fixy::Sink::Err, "the set of an interleave of the thread failed (errno {})\n",
                     foundation::os::error_number_of(set));
        return 1;
    }
    bool const is_interleave = fixy::numa::read_thread_memory_policy(ctx).match(
        [&interleave](ThreadMemoryPolicy read) noexcept { return read == interleave; },
        [](Errno) noexcept { return false; });
    bool const is_back = fixy::numa::set_thread_memory_policy(ctx, first).is_ok();
    bool const is_first = fixy::numa::read_thread_memory_policy(ctx).match(
        [&first](ThreadMemoryPolicy read) noexcept { return read == first; }, [](Errno) noexcept { return false; });
    if (!is_interleave || !is_back || !is_first) {
        fixy::report(fixy::Sink::Err, "the thread policy did not read back as set (interleave {}, back {}, first {})\n",
                     is_interleave, is_back, is_first);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    int const refusals = the_mint_refuses_a_sentinel_node() + the_mint_refuses_an_empty_region()
                       + the_mint_refuses_a_node_the_machine_lacks() + the_policy_mint_refuses_a_bad_mask()
                       + the_allowed_nodes_are_not_empty() + the_thread_policy_door_refuses_a_bad_policy();
    if (refusals != 0) return 1;
    int const thread_policy = the_thread_policy_reads_back_as_set();
    if (thread_policy != 0 && thread_policy != kSkip) return 1;
    int const interleaved = an_interleaved_region_spreads_over_its_nodes();
    if (interleaved != 0 && interleaved != kSkip) return 1;
    int const preferred = a_preferred_region_is_on_its_node();
    if (preferred != 0 && preferred != kSkip) return 1;

    int const bound = a_bound_region_is_on_its_node();
    if (bound == kSkip) return 0;
    if (bound != 0) return 1;
    int const admits = the_proof_admits_only_its_node();
    if (admits != 0 && admits != kSkip) return 1;

    std::printf("test_os_numa_placement: a bound region is on its node, and the mint refuses three bad calls\n");
    return 0;
}
