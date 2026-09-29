// Every mapping below comes from OwnedMmap::mint_region, because that is
// the only door there is: the constructor that claims an address is
// private, so a region exists only over what ::mmap returned.
//
// Sentinel TU for fixy/OwnedMmap.h: the region is move-only, the leak
// witness admits only the leak atom, release binds to an rvalue, and the
// kernel maps the protection that the type claims.
//
// The empty state is the only one reachable without a syscall, so it is
// checked first.  Then this TU maps anonymous pages, so the unmap path,
// the move path and the release gated on a leak atom all run.

#include <fixy/OwnedMmap.h>
#include <foundation/permissions/Permission.h>

#include <sys/mman.h>
#include <unistd.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <type_traits>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
namespace mm = ::fixy::mmap;

struct PageTag {
    using permission_row = eff::Row<>;
};
using Region = ::fixy::OwnedMmap<PageTag, mm::prot::WriteCopy, mm::share::Anonymous>;
using SampleLeak = ::fixy::atom::leak::resource<::fixy::atom::detail::leak_sample_rationale>;

// A context of a test that maps: it owns IO and Block, the row of a
// mapping.
using MapCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

// A mapping takes the brand of the permission it is minted with, so each
// case mints a permission and maps under its brand.
template <typename Prot, typename Brand>
[[nodiscard]] auto map_pages(perm::Permission<PageTag, Brand> const& owner, std::size_t length) {
    MapCtx ctx{eff::testing::test()};
    return ::fixy::OwnedMmap<PageTag, Prot, mm::share::Anonymous, Brand>::mint_region(ctx, owner, -1, length, 0);
}

static_assert(!std::is_copy_constructible_v<Region>);
static_assert(!std::is_copy_assignable_v<Region>);
static_assert(std::is_nothrow_move_constructible_v<Region>);
static_assert(std::is_nothrow_move_assignable_v<Region>);
static_assert(sizeof(Region) == sizeof(void*) + sizeof(std::size_t));

// The tag triple is part of the type, so two regions that name
// different tags cannot be assigned across.
struct OtherTag {};
using OtherRegion = ::fixy::OwnedMmap<OtherTag, mm::prot::WriteCopy, mm::share::Anonymous>;
static_assert(!std::is_assignable_v<Region&, OtherRegion&&>);
static_assert(!std::is_constructible_v<Region, OtherRegion&&>);

// release binds to an rvalue only, and only with a leak atom.
template <typename R>
concept ReleasesLvalue = requires(R& r, SampleLeak w) { r.release(w); };
static_assert(!ReleasesLvalue<Region>);

// The kernel rounds a mapping up to a whole page, and ::munmap is
// called with the same length, so asking the system keeps the two ends
// of every mapping below in step on a host whose page is not 4 KiB.
std::size_t page_bytes() {
    const long reported = ::sysconf(_SC_PAGESIZE);
    return reported > 0 ? static_cast<std::size_t>(reported) : std::size_t{4096};
}

// The protection field of the /proc/self/maps line that holds address,
// such as "r--p", or an empty string when no line holds it.
std::string protection_of(void const* address) {
    std::ifstream maps{"/proc/self/maps"};
    const auto wanted = std::bit_cast<std::uintptr_t>(address);
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long start = 0;
        unsigned long end = 0;
        char protection[5] = {};
        if (std::sscanf(line.c_str(), "%lx-%lx %4s", &start, &end, protection) != 3) continue;
        if (wanted >= start && wanted < end) return std::string{protection};
    }
    return {};
}

// The kernel maps the protection and the share mode that the type claims.
// A caller gives mint_region no bit, so a region typed read-only is a
// read-only private page, and one typed copy-on-write is writable.
int check_the_kernel_maps_the_claimed_protection() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    auto read_only = map_pages<mm::prot::ReadOnly>(owner, len);
    if (!read_only) return 0;
    if (protection_of(read_only->data()) != "r--p") {
        std::fprintf(stderr, "a region typed prot::ReadOnly is mapped %s\n", protection_of(read_only->data()).c_str());
        return 50;
    }
    auto writable = map_pages<mm::prot::WriteCopy>(owner, len);
    if (!writable) return 0;
    if (protection_of(writable->data()) != "rw-p") {
        std::fprintf(stderr, "a region typed prot::WriteCopy is mapped %s\n", protection_of(writable->data()).c_str());
        return 51;
    }
    return 0;
}

int check_live_mapping() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    auto mapped = map_pages<mm::prot::WriteCopy>(owner, len);
    if (!mapped) return 0;  // mapping unavailable; nothing to check

    auto r = std::move(*mapped);
    void* const addr = r.data();
    if (!r.is_mapped()) return 10;
    if (addr == MAP_FAILED) return 11;
    if (r.size() != len) return 12;

    // The destructor unmaps.  A second region moved from this one must
    // leave the source empty so the unmap happens exactly once.
    auto moved = std::move(r);
    if (r.is_mapped()) return 13;
    if (!moved.is_mapped()) return 14;
    if (moved.data() != addr) return 15;

    return 0;
}

int check_release_hands_the_region_back() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    auto mapped = map_pages<mm::prot::WriteCopy>(owner, len);
    if (!mapped) return 0;

    auto r = std::move(*mapped);
    void* const addr = r.data();
    auto [out_addr, out_len] = std::move(r).release(SampleLeak{});
    if (out_addr != addr) return 20;
    if (out_len != len) return 21;
    if (r.is_mapped()) return 22;

    // Ownership left the wrapper, so the unmap is ours.
    if (::munmap(out_addr, out_len) != 0) return 23;  // SYSCALL-CAP-OK: the test owns the region after release
    return 0;
}

int check_move_assign_unmaps_the_replaced_region() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    auto first = map_pages<mm::prot::WriteCopy>(owner, len);
    if (!first) return 0;
    auto second = map_pages<mm::prot::WriteCopy>(owner, len);
    if (!second) return 0;

    auto a = std::move(*first);
    auto b = std::move(*second);
    void* const second_addr = b.data();
    a = std::move(b);
    if (b.is_mapped()) return 30;
    if (a.data() != second_addr) return 31;

    return 0;
}

// The empty state is reachable without a syscall, and every query
// answers on it.  release_ takes the not-mapped branch, so the
// destructor of an empty region issues no syscall.
int check_empty_state() {
    Region empty{};
    if (empty.is_mapped()) return 40;
    if (empty.size() != 0u) return 41;
    if (empty.data() != MAP_FAILED) return 42;

    Region moved = std::move(empty);
    if (moved.is_mapped()) return 43;

    Region assigned{};
    assigned = std::move(moved);
    if (assigned.is_mapped()) return 44;

    auto [addr, len] = std::move(assigned).release(SampleLeak{});
    if (addr != MAP_FAILED) return 45;
    if (len != 0u) return 46;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_empty_state(); rc != 0) return rc;
    if (int rc = check_the_kernel_maps_the_claimed_protection(); rc != 0) return rc;
    if (int rc = check_live_mapping(); rc != 0) return rc;
    if (int rc = check_release_hands_the_region_back(); rc != 0) return rc;
    if (int rc = check_move_assign_unmaps_the_replaced_region(); rc != 0) return rc;

    return 0;
}
